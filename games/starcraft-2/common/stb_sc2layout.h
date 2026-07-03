/*
 * stb_sc2layout.h — SC2 .SC2Layout XML parser and frame builder (single-header).
 *
 * Parses StarCraft II UI layout files (.SC2Layout) into sc2BaseFrame_t arrays.
 * Uses libxml2 for XML parsing.
 *
 * Declarations-only mode (default):
 *   Include this header normally to get types and extern declarations.
 *
 * Implementation mode:
 *   #define STB_SC2LAYOUT_IMPLEMENTATION before including this header in exactly
 *   one .c file to get the full parser and layout builder.
 *
 * Host services required (provided by the host module):
 *   uiimport for FS_ReadFile, FS_FreeFile, FontIndex, ImageIndex,
 *   GetRenderer, GetTexture, GetFont.
 */
#ifndef stb_sc2layout_h
#define stb_sc2layout_h

#include "common/shared.h"
#include "client/ui.h"

/* -------------------------------------------------------------------------- */
/* Constants                                                                   */
/* -------------------------------------------------------------------------- */
#define SC2_MAX_FRAMES     4096
#define SC2_MAX_TEMPLATES  1024
#define SC2_MAX_INCLUDES   128
#define SC2_MAX_ANCHORS    4
#define SC2_MAX_CONSTANTS  256
#define SC2_MAX_TEXTURES   8
#define SC2_MAX_CHILDREN   64

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

/* SC2 texture flags */
#define SC2_TEX_TILED                   (1 << 0)
#define SC2_TEX_HAS_TEXTURE             (1 << 1)

/* SC2 anchor flags */
#define SC2_ANCHOR_HAS                  (1 << 0)

/* SC2 backdrop flags */
#define SC2_BACKDROP_TILE               (1 << 0)

/* UI flags for sc2BaseFrame_t */
#define SC2_UIF_HIDDEN   (1 << 10)
#define SC2_UIF_DISABLED (1 << 11)
#define SC2_UIF_PRESSED  (1 << 12)
#define SC2_UIF_HOVERED  (1 << 13)

/* -------------------------------------------------------------------------- */
/* SC2 anchor side indices                                                     */
/* -------------------------------------------------------------------------- */
typedef enum {
    SC2_SIDE_TOP,
    SC2_SIDE_BOTTOM,
    SC2_SIDE_LEFT,
    SC2_SIDE_RIGHT,
    SC2_SIDE_COUNT,
} SC2Side;

/* SC2 anchor position */
typedef enum {
    SC2_POS_MIN,
    SC2_POS_MID,
    SC2_POS_MAX,
    SC2_POS_COUNT,
} SC2Pos;

/* SC2 frame type */
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

/* -------------------------------------------------------------------------- */
/* Flattened frame types                                                       */
/* -------------------------------------------------------------------------- */
typedef struct {
    uiFramePointPos_t targetPos;
    BOOL used;
    DWORD relative_index;
    FLOAT offset;
    LPCSTR relative_name;   /* non-NULL = unresolved named-relative (pending post-pass) */
} sc2BaseFramePoint_t;

typedef sc2BaseFramePoint_t sc2BaseFramePoints_t[FPP_COUNT];

typedef struct sc2BaseFrame_s {
    DWORD number;
    FRAMETYPE type;
    DWORD sc2_type;
    LPCSTR name;
    void *parent;
    DWORD parent_index;
    struct { sc2BaseFramePoints_t x, y; } points;
    RECT screen_rect;
    COLOR32 color;
    FLOAT alpha;
    struct { FLOAT width, height; } size;
    DWORD image;
    RECT texcoord;
    LPCSTR text;
    DWORD stat;
    uiLabel_t label;
    COLOR32 text_color;
    struct {
        DWORD bg;
        DWORD edge;
        DWORD flags;
        FLOAT insets[4];
    } backdrop;
    DWORD ui_flags;
    void (*on_event)(struct sc2BaseFrame_s *frame, FLOAT x, FLOAT y, int button, BOOL down);
    void (*on_draw)(struct sc2BaseFrame_s *frame, LPCRECT rect);
} sc2BaseFrame_t;

typedef sc2BaseFrame_t *LPSC2BASEFRAME;
typedef sc2BaseFrame_t const *LPCSC2BASEFRAME;

/* -------------------------------------------------------------------------- */
/* Parsed frame types (before flattening)                                      */
/* -------------------------------------------------------------------------- */
typedef struct {
    SC2Side side;
    SC2Pos pos;
    int16_t offset;
    UINAME relative;
    DWORD flags;
} sc2ParsedAnchor_t;

typedef struct {
    UINAME resource;
    UINAME texture_type;
    int layer;
    DWORD flags;
} sc2ParsedTexture_t;

typedef struct {
    UINAME name;
    char val[64];
} sc2Constant_t;

typedef struct sc2Frame_s {
    UINAME name;
    sc2FrameType type;
    UINAME template_path;
    UINAME image_ref;
    FLOAT width, height;
    DWORD flags;
    COLOR32 color;
    FLOAT alpha;
    sc2ParsedAnchor_t anchors[SC2_MAX_ANCHORS];
    int num_anchors;
    sc2ParsedTexture_t textures[SC2_MAX_TEXTURES];
    int num_textures;
    struct sc2Frame_s *children[SC2_MAX_CHILDREN];
    int num_children;
    struct sc2Frame_s *parent;
    sc2BaseFrame_t *resolved_frame;
    PATHSTR source_file;
} sc2Frame_t;

typedef struct {
    sc2Frame_t templates[SC2_MAX_TEMPLATES];
    int num_templates;
    sc2Constant_t constants[SC2_MAX_CONSTANTS];
    int num_constants;
    sc2BaseFrame_t frames[SC2_MAX_FRAMES];
    int num_frames;
    sc2Frame_t *root;
    PATHSTR included_files[SC2_MAX_INCLUDES];
    int num_includes;
} sc2Layout_t;

/* -------------------------------------------------------------------------- */
/* Bind macros                                                                 */
/* -------------------------------------------------------------------------- */
#ifndef BZ_SC2_REPORT_MISSING
#define BZ_SC2_REPORT_MISSING(NAME) \
    fprintf(stderr, "SC2_Layout: missing frame '%s' in %s\n", (NAME), __FUNCTION__)
#endif

/* -------------------------------------------------------------------------- */
/* API declarations                                                           */
/* -------------------------------------------------------------------------- */
void         SC2_LayoutInit(void);
void         SC2_LayoutShutdown(void);
BOOL         SC2_LayoutParseFile(LPCSTR filename);
BOOL         SC2_LayoutFlatten(LPCSTR root_name);
BOOL         SC2_LayoutBuildMainMenu(void);
BOOL         SC2_LayoutBuildGameUI(void);
sc2BaseFrame_t *SC2_LayoutGetFrames(DWORD *count);
sc2Frame_t   *SC2_LayoutFindTemplate(LPCSTR name);
sc2BaseFrame_t *SC2_LayoutFindFrameByType(sc2FrameType type);
sc2BaseFrame_t *SC2_LayoutFindFrameByName(LPCSTR name);
sc2BaseFrame_t *SC2_LayoutFindChildFrame(sc2BaseFrame_t *parent, LPCSTR name);
int           SC2_LayoutNumTemplates(void);
sc2Frame_t   *SC2_LayoutGetTemplate(int index);
LPCSTR        SC2_LayoutResolveConstant(LPCSTR name);
FRAMETYPE     SC2_MapFrameType(sc2FrameType sc2_type);

void SC2_InitFrame(sc2Frame_t *frame, sc2FrameType type);
void SC2_SetSize(sc2Frame_t *frame, FLOAT width, FLOAT height);
void SC2_SetHidden(sc2Frame_t *frame, BOOL value);
void SC2_SetEnabled(sc2Frame_t *frame, BOOL enabled);

#endif /* stb_sc2layout_h */

/* ========================================================================== */
/* IMPLEMENTATION                                                             */
/* ========================================================================== */
#ifdef STB_SC2LAYOUT_IMPLEMENTATION
#ifndef stb_sc2layout_impl
#define stb_sc2layout_impl

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

extern uiImport_t uiimport;

#ifndef CLAMP
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#endif

#ifndef STB_SC2LAYOUT_GLOBALS
extern sc2Layout_t sc2_layout;
#else
sc2Layout_t sc2_layout = { 0 };
#endif

static void SC2_Strncpyz(char *dst, LPCSTR src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static LPCSTR SC2_XmlGetProp(void *node, LPCSTR name) {
    xmlChar *val = xmlGetProp((xmlNode *)node, (const xmlChar *)name);
    return (const char *)val;
}

static void SC2_XmlFree(LPCSTR s) {
    if (s) xmlFree((xmlChar *)s);
}

static struct { LPCSTR name; sc2FrameType type; } sc2_frame_types[] = {
    { "Frame",                  SC2_FRAMETYPE_FRAME },
    { "Button",                 SC2_FRAMETYPE_BUTTON },
    { "Image",                  SC2_FRAMETYPE_IMAGE },
    { "Label",                  SC2_FRAMETYPE_LABEL },
    { "EditBox",                SC2_FRAMETYPE_EDITBOX },
    { "Tooltip",                SC2_FRAMETYPE_TOOLTIP },
    { "Model",                  SC2_FRAMETYPE_MODEL },
    { "ConsolePanel",           SC2_FRAMETYPE_CONSOLE_PANEL },
    { "CommandPanel",           SC2_FRAMETYPE_COMMAND_PANEL },
    { "CommandButton",          SC2_FRAMETYPE_COMMAND_BUTTON },
    { "CommandTooltip",         SC2_FRAMETYPE_COMMAND_TOOLTIP },
    { "InfoPanel",              SC2_FRAMETYPE_INFO_PANEL },
    { "MinimapPanel",           SC2_FRAMETYPE_MINIMAP_PANEL },
    { "Minimap",                SC2_FRAMETYPE_MINIMAP },
    { "ResourcePanel",          SC2_FRAMETYPE_RESOURCE_PANEL },
    { "PortraitPanel",          SC2_FRAMETYPE_PORTRAIT_PANEL },
    { "GameUI",                 SC2_FRAMETYPE_GAME_UI },
    { "WorldPanel",             SC2_FRAMETYPE_WORLD_PANEL },
    { "CountdownLabel",         SC2_FRAMETYPE_COUNTDOWN_LABEL },
    { "HeroPanel",              SC2_FRAMETYPE_HERO_PANEL },
    { "InventoryPanel",         SC2_FRAMETYPE_INVENTORY_PANEL },
    { "IdleButton",             SC2_FRAMETYPE_IDLE_BUTTON },
    { "AIButton",               SC2_FRAMETYPE_AI_BUTTON },
    { "PylonButton",            SC2_FRAMETYPE_PYLON_BUTTON },
    { "MessageDisplay",         SC2_FRAMETYPE_MESSAGE_DISPLAY },
    { "AlertPanel",             SC2_FRAMETYPE_ALERT_PANEL },
    { "AlertDisplay",           SC2_FRAMETYPE_ALERT_DISPLAY },
    { "ChatBar",                SC2_FRAMETYPE_CHAT_BAR },
    { "MenuBar",                SC2_FRAMETYPE_MENU_BAR },
    { "SubtitlePanel",          SC2_FRAMETYPE_SUBTITLE_PANEL },
    { "TimePanel",              SC2_FRAMETYPE_TIME_PANEL },
    { "CreditsPanel",           SC2_FRAMETYPE_CREDITS_PANEL },
    { "ControlGroupPanel",      SC2_FRAMETYPE_CONTROL_GROUP_PANEL },
    { "MinimapPanelTooltip",    SC2_FRAMETYPE_MINIMAP_PANEL_TOOLTIP },
    { "RevealPanel",            SC2_FRAMETYPE_REVEAL_PANEL },
    { "AlliancePanel",          SC2_FRAMETYPE_ALLIANCE_PANEL },
    { "TeamResourcePanel",      SC2_FRAMETYPE_TEAM_RESOURCE_PANEL },
    { "LeaderPanel",            SC2_FRAMETYPE_LEADER_PANEL },
    { "ObserverPanelContainer", SC2_FRAMETYPE_OBSERVER_PANEL },
    { "ReplayPanelContainer",   SC2_FRAMETYPE_REPLAY_PANEL },
    { "TalkerPanel",            SC2_FRAMETYPE_TALKER_PANEL },
    { "PurchasePanel",          SC2_FRAMETYPE_PURCHASE_PANEL },
    { "ResearchPanel",          SC2_FRAMETYPE_RESEARCH_PANEL },
    { "PlanetPanelContainer",   SC2_FRAMETYPE_PLANET_PANEL },
    { "CashPanel",              SC2_FRAMETYPE_CASH_PANEL },
    { "ResourceRequestAlertPanel", SC2_FRAMETYPE_RESOURCE_REQUEST_ALERT },
    { "PausePanel",             SC2_FRAMETYPE_PAUSE_PANEL },
    { "ConversationPanel",      SC2_FRAMETYPE_CONVERSATION_PANEL },
    { "ObjectivePanel",         SC2_FRAMETYPE_OBJECTIVE_PANEL },
    { "TriggerDialogFrame",     SC2_FRAMETYPE_TRIGGER_DIALOG },
    { "TriggerWindowPanel",     SC2_FRAMETYPE_TRIGGER_WINDOW },
    { "SystemAlertPanel",       SC2_FRAMETYPE_SYSTEM_ALERT_PANEL },
    { "TipAlertPanel",          SC2_FRAMETYPE_TIP_ALERT },
    { "TipAlertMovingFrame",    SC2_FRAMETYPE_TIP_ALERT_MOVING },
    { "CinematicTextPanel",     SC2_FRAMETYPE_CINEMATIC_TEXT },
    { "TextCrawlPanel",         SC2_FRAMETYPE_TEXT_CRAWL },
    { "FlashFrame",             SC2_FRAMETYPE_FLASH_FRAME },
    { "LeroyButton",            SC2_FRAMETYPE_LEROY_BUTTON },
    { "MapInfoPanel",           SC2_FRAMETYPE_MAP_INFO_PANEL },
    { "PerfOverlay",            SC2_FRAMETYPE_PERF_OVERLAY },
    { "ProfilerOptions",        SC2_FRAMETYPE_PROFILER },
    { "AchievementPanel",       SC2_FRAMETYPE_ACHIEVEMENT_PANEL },
    { "ScreenCredits",          SC2_FRAMETYPE_SCREEN_CREDITS },
    { NULL, SC2_FRAMETYPE_UNKNOWN },
};

static struct { LPCSTR name; SC2Side side; } sc2_sides[] = {
    { "Top",    SC2_SIDE_TOP },
    { "Bottom", SC2_SIDE_BOTTOM },
    { "Left",   SC2_SIDE_LEFT },
    { "Right",  SC2_SIDE_RIGHT },
    { NULL, -1 },
};

static struct { LPCSTR name; SC2Pos pos; } sc2_positions[] = {
    { "Min", SC2_POS_MIN },
    { "Mid", SC2_POS_MID },
    { "Max", SC2_POS_MAX },
    { NULL, -1 },
};

static sc2FrameType SC2_LookupFrameType(LPCSTR name) {
    for (int i = 0; sc2_frame_types[i].name; i++)
        if (!strcasecmp(sc2_frame_types[i].name, name))
            return sc2_frame_types[i].type;
    return SC2_FRAMETYPE_UNKNOWN;
}

static SC2Side SC2_LookupSide(LPCSTR name) {
    for (int i = 0; sc2_sides[i].name; i++)
        if (!strcasecmp(sc2_sides[i].name, name))
            return sc2_sides[i].side;
    return -1;
}

static SC2Pos SC2_LookupPos(LPCSTR name) {
    for (int i = 0; sc2_positions[i].name; i++)
        if (!strcasecmp(sc2_positions[i].name, name))
            return sc2_positions[i].pos;
    return -1;
}

static BOOL xmlGetAttrBool(void *node, LPCSTR name, BOOL *out) {
    LPCSTR val = SC2_XmlGetProp(node, name);
    if (!val) return false;
    *out = (!strcasecmp(val, "true") || !strcasecmp(val, "1") || !strcasecmp(val, "True"));
    SC2_XmlFree(val);
    return true;
}

static BOOL xmlGetAttrFloat(void *node, LPCSTR name, FLOAT *out) {
    LPCSTR val = SC2_XmlGetProp(node, name);
    if (!val) return false;
    *out = (FLOAT)atof(val);
    SC2_XmlFree(val);
    return true;
}

static BOOL xmlGetAttrInt(void *node, LPCSTR name, int *out) {
    LPCSTR val = SC2_XmlGetProp(node, name);
    if (!val) return false;
    *out = atoi(val);
    SC2_XmlFree(val);
    return true;
}

static COLOR32 SC2_ParseColor(LPCSTR str) {
    COLOR32 c = { 255, 255, 255, 255 };
    if (!str) return c;
    int r = 0, g = 0, b = 0, a = 255;
    sscanf(str, "%d,%d,%d,%d", &r, &g, &b, &a);
    c.r = (BYTE)CLAMP(r, 0, 255);
    c.g = (BYTE)CLAMP(g, 0, 255);
    c.b = (BYTE)CLAMP(b, 0, 255);
    c.a = (BYTE)CLAMP(a, 0, 255);
    return c;
}

static sc2Frame_t *SC2_FindTemplate(LPCSTR name) {
    if (!name) return NULL;
    for (int i = 0; i < sc2_layout.num_templates; i++)
        if (!strcasecmp(sc2_layout.templates[i].name, name))
            return &sc2_layout.templates[i];
    return NULL;
}

static sc2Frame_t *SC2_AddTemplate(void) {
    if (sc2_layout.num_templates >= SC2_MAX_TEMPLATES) {
        fprintf(stderr, "SC2_Layout: template overflow\n");
        return NULL;
    }
    return &sc2_layout.templates[sc2_layout.num_templates++];
}

static void SC2_AddConstant(LPCSTR name, LPCSTR val) {
    if (sc2_layout.num_constants >= SC2_MAX_CONSTANTS) return;
    sc2Constant_t *c = &sc2_layout.constants[sc2_layout.num_constants++];
    SC2_Strncpyz(c->name, name, sizeof(c->name));
    SC2_Strncpyz(c->val, val, sizeof(c->val));
}

LPCSTR SC2_LayoutResolveConstant(LPCSTR name) {
    if (!name || name[0] != '#') return name;
    while (*name == '#') name++;
    for (int i = 0; i < sc2_layout.num_constants; i++)
        if (!strcasecmp(sc2_layout.constants[i].name, name))
            return sc2_layout.constants[i].val;
    return NULL;
}

static void SC2_ParseFrameAttrs(void *node, sc2Frame_t *frame);
static void SC2_ParseFrameChildren(void *node, sc2Frame_t *frame);

static void SC2_ResolveTemplate(sc2Frame_t *frame, sc2Frame_t *tmpl) {
    if (!tmpl) return;

    if (!(frame->flags & SC2_FRAME_HAS_WIDTH) && (tmpl->flags & SC2_FRAME_HAS_WIDTH)) {
        frame->width = tmpl->width;
        frame->flags |= SC2_FRAME_HAS_WIDTH;
    }
    if (!(frame->flags & SC2_FRAME_HAS_HEIGHT) && (tmpl->flags & SC2_FRAME_HAS_HEIGHT)) {
        frame->height = tmpl->height;
        frame->flags |= SC2_FRAME_HAS_HEIGHT;
    }
    if (!(frame->flags & SC2_FRAME_HAS_VISIBLE) && (tmpl->flags & SC2_FRAME_HAS_VISIBLE)) {
        if (tmpl->flags & SC2_FRAME_VISIBLE) frame->flags |= SC2_FRAME_VISIBLE;
        else frame->flags &= ~SC2_FRAME_VISIBLE;
        frame->flags |= SC2_FRAME_HAS_VISIBLE;
    }
    if (!(frame->flags & SC2_FRAME_HAS_COLOR) && (tmpl->flags & SC2_FRAME_HAS_COLOR)) {
        frame->color = tmpl->color;
        frame->flags |= SC2_FRAME_HAS_COLOR;
    }
    if (!(frame->flags & SC2_FRAME_HAS_ALPHA) && (tmpl->flags & SC2_FRAME_HAS_ALPHA)) {
        frame->alpha = tmpl->alpha;
        frame->flags |= SC2_FRAME_HAS_ALPHA;
    }
    if (!(frame->flags & SC2_FRAME_ACCEPTS_MOUSE)) {
        if (tmpl->flags & SC2_FRAME_ACCEPTS_MOUSE) frame->flags |= SC2_FRAME_ACCEPTS_MOUSE;
        else frame->flags &= ~SC2_FRAME_ACCEPTS_MOUSE;
    }
    if (!(frame->flags & SC2_FRAME_COLLAPSE_LAYOUT)) {
        if (tmpl->flags & SC2_FRAME_COLLAPSE_LAYOUT) frame->flags |= SC2_FRAME_COLLAPSE_LAYOUT;
        else frame->flags &= ~SC2_FRAME_COLLAPSE_LAYOUT;
    }
    if (!(frame->flags & SC2_FRAME_HIGHLIGHT_ON_HOVER)) {
        if (tmpl->flags & SC2_FRAME_HIGHLIGHT_ON_HOVER) frame->flags |= SC2_FRAME_HIGHLIGHT_ON_HOVER;
        else frame->flags &= ~SC2_FRAME_HIGHLIGHT_ON_HOVER;
    }
    if (!(frame->flags & SC2_FRAME_HIGHLIGHT_ON_FOCUS)) {
        if (tmpl->flags & SC2_FRAME_HIGHLIGHT_ON_FOCUS) frame->flags |= SC2_FRAME_HIGHLIGHT_ON_FOCUS;
        else frame->flags &= ~SC2_FRAME_HIGHLIGHT_ON_FOCUS;
    }

    if (tmpl->num_anchors > 0) {
        int num = 0;
        sc2ParsedAnchor_t merged[SC2_MAX_ANCHORS];
        for (int i = 0; i < tmpl->num_anchors && num < SC2_MAX_ANCHORS; i++)
            merged[num++] = tmpl->anchors[i];
        for (int i = 0; i < frame->num_anchors && num < SC2_MAX_ANCHORS; i++) {
            BOOL found = false;
            for (int j = 0; j < num; j++) {
                if (merged[j].side == frame->anchors[i].side) {
                    merged[j] = frame->anchors[i];
                    found = true;
                    break;
                }
            }
            if (!found) merged[num++] = frame->anchors[i];
        }
        memcpy(frame->anchors, merged, sizeof(merged));
        frame->num_anchors = num;
    }

    if (frame->num_textures == 0 && tmpl->num_textures > 0) {
        for (int i = 0; i < tmpl->num_textures; i++) {
            frame->textures[i] = tmpl->textures[i];
            frame->num_textures++;
        }
    }

    for (int i = 0; i < tmpl->num_children && frame->num_children < SC2_MAX_CHILDREN; i++) {
        sc2Frame_t *clone = SC2_AddTemplate();
        if (!clone) break;
        *clone = *tmpl->children[i];
        clone->template_path[0] = '\0';
        clone->parent = frame;
        clone->resolved_frame = NULL;
        frame->children[frame->num_children++] = clone;
    }
}

static sc2Frame_t *SC2_ResolveTemplatePath(LPCSTR path) {
    if (!path) return NULL;
    sc2Frame_t *t = SC2_FindTemplate(path);
    if (t) return t;
    LPCSTR slash = strrchr(path, '/');
    if (slash) {
        t = SC2_FindTemplate(slash + 1);
        if (t) return t;
    }
    return NULL;
}

static void SC2_ParseInclude(void *node) {
    LPCSTR path = SC2_XmlGetProp(node, "path");
    if (!path) return;
    for (int i = 0; i < sc2_layout.num_includes; i++) {
        if (!strcasecmp(sc2_layout.included_files[i], path)) {
            SC2_XmlFree(path);
            return;
        }
    }
    if (sc2_layout.num_includes < SC2_MAX_INCLUDES)
        SC2_Strncpyz(sc2_layout.included_files[sc2_layout.num_includes++], path, MAX_PATHLEN);
    SC2_LayoutParseFile(path);
    SC2_XmlFree(path);
}

static void SC2_ResolveIncludes(void *node) {
    for (xmlNode *cur = (xmlNode *)node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        if (!strcasecmp((const char *)cur->name, "Include"))
            SC2_ParseInclude(cur);
    }
}

static void SC2_ParseConstant(void *node) {
    LPCSTR name = SC2_XmlGetProp(node, "name");
    LPCSTR val = SC2_XmlGetProp(node, "val");
    if (name && val) SC2_AddConstant(name, val);
    if (name) SC2_XmlFree(name);
    if (val) SC2_XmlFree(val);
}

static int SC2_ResolveAttrInt(void *node, LPCSTR attr, int default_val) {
    LPCSTR raw = SC2_XmlGetProp(node, attr);
    if (!raw) return default_val;
    LPCSTR resolved = SC2_LayoutResolveConstant(raw);
    int result = atoi(resolved ? resolved : raw);
    SC2_XmlFree(raw);
    return result;
}

static FLOAT SC2_ResolveAttrFloat(void *node, LPCSTR attr, FLOAT default_val) {
    LPCSTR raw = SC2_XmlGetProp(node, attr);
    if (!raw) return default_val;
    LPCSTR resolved = SC2_LayoutResolveConstant(raw);
    FLOAT result = (FLOAT)atof(resolved ? resolved : raw);
    SC2_XmlFree(raw);
    return result;
}

static void SC2_ParseAnchor(void *node, sc2Frame_t *frame) {
    if (frame->num_anchors >= SC2_MAX_ANCHORS) return;

    LPCSTR side_str = SC2_XmlGetProp(node, "side");
    LPCSTR pos_str = SC2_XmlGetProp(node, "pos");
    LPCSTR relative = SC2_XmlGetProp(node, "relative");

    if ((!side_str || !pos_str) && relative) {
        static struct { LPCSTR side; LPCSTR pos; } const sides[] = {
            { "Top", "Min" }, { "Bottom", "Max" }, { "Left", "Min" }, { "Right", "Max" },
        };
        for (int i = 0; i < 4 && frame->num_anchors < SC2_MAX_ANCHORS; i++) {
            sc2ParsedAnchor_t *a = &frame->anchors[frame->num_anchors];
            a->side = SC2_LookupSide(sides[i].side);
            a->pos = SC2_LookupPos(sides[i].pos);
            a->offset = (int16_t)SC2_ResolveAttrInt(node, "offset", 0);
            SC2_Strncpyz(a->relative, relative, sizeof(a->relative));
            a->flags |= SC2_ANCHOR_HAS;
            frame->num_anchors++;
        }
        if (side_str) SC2_XmlFree(side_str);
        if (pos_str) SC2_XmlFree(pos_str);
        SC2_XmlFree(relative);
        return;
    }

    if (!side_str || !pos_str) {
        if (side_str) SC2_XmlFree(side_str);
        if (pos_str) SC2_XmlFree(pos_str);
        if (relative) SC2_XmlFree(relative);
        return;
    }

    sc2ParsedAnchor_t *a = &frame->anchors[frame->num_anchors];
    a->side = SC2_LookupSide(side_str);
    a->pos = SC2_LookupPos(pos_str);
    a->offset = (int16_t)SC2_ResolveAttrInt(node, "offset", 0);
    if (relative) {
        SC2_Strncpyz(a->relative, relative, sizeof(a->relative));
        SC2_XmlFree(relative);
    } else {
        SC2_Strncpyz(a->relative, "$parent", sizeof(a->relative));
    }
    a->flags |= SC2_ANCHOR_HAS;
    frame->num_anchors++;

    SC2_XmlFree(side_str);
    SC2_XmlFree(pos_str);
}

static void SC2_ParseTexture(void *node, sc2Frame_t *frame, int layer_override) {
    int layer = layer_override;
    xmlGetAttrInt(node, "layer", &layer);
    if (layer < 0 || layer >= SC2_MAX_TEXTURES) layer = 0;

    sc2ParsedTexture_t *tex = &frame->textures[layer];
    LPCSTR val = SC2_XmlGetProp(node, "val");
    if (val) {
        SC2_Strncpyz(tex->resource, val, sizeof(tex->resource));
        SC2_XmlFree(val);
        tex->flags |= SC2_TEX_HAS_TEXTURE;
    }
    tex->layer = layer;

    LPCSTR tiled = SC2_XmlGetProp(node, "tiled");
    if (tiled) {
        if (!strcasecmp(tiled, "true") || !strcasecmp(tiled, "1"))
            tex->flags |= SC2_TEX_TILED;
        else
            tex->flags &= ~SC2_TEX_TILED;
        SC2_XmlFree(tiled);
    }

    if (frame->num_textures <= layer)
        frame->num_textures = layer + 1;
}

static void SC2_ParseModel(void *node, sc2Frame_t *frame) {
    LPCSTR val = SC2_XmlGetProp(node, "val");
    if (val) {
        sc2ParsedTexture_t *tex = &frame->textures[0];
        SC2_Strncpyz(tex->resource, val, sizeof(tex->resource));
        tex->flags |= SC2_TEX_HAS_TEXTURE;
        frame->num_textures = 1;
        SC2_XmlFree(val);
    }
}

static void SC2_ParseFrameAttrs(void *node, sc2Frame_t *frame) {
    LPCSTR type_str = SC2_XmlGetProp(node, "type");
    if (type_str) {
        frame->type = SC2_LookupFrameType(type_str);
        SC2_XmlFree(type_str);
    } else {
        frame->type = SC2_FRAMETYPE_FRAME;
    }

    LPCSTR name = SC2_XmlGetProp(node, "name");
    if (name) {
        SC2_Strncpyz(frame->name, name, sizeof(frame->name));
        SC2_XmlFree(name);
    }

    LPCSTR tmpl = SC2_XmlGetProp(node, "template");
    if (tmpl) {
        SC2_Strncpyz(frame->template_path, tmpl, sizeof(frame->template_path));
        SC2_XmlFree(tmpl);
    }

    LPCSTR image = SC2_XmlGetProp(node, "Image");
    if (image) {
        SC2_Strncpyz(frame->image_ref, image, sizeof(frame->image_ref));
        SC2_XmlFree(image);
    }

    frame->resolved_frame = NULL;
}

static void SC2_ParseFrameChildren(void *node, sc2Frame_t *frame) {
    for (xmlNode *cur = ((xmlNode *)node)->children; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        LPCSTR tag = (const char *)cur->name;

        if (!strcasecmp(tag, "Width")) {
            frame->width = SC2_ResolveAttrFloat(cur, "val", 0.0f);
            frame->flags |= SC2_FRAME_HAS_WIDTH;
        } else if (!strcasecmp(tag, "Height")) {
            frame->height = SC2_ResolveAttrFloat(cur, "val", 0.0f);
            frame->flags |= SC2_FRAME_HAS_HEIGHT;
        } else if (!strcasecmp(tag, "Anchor")) {
            SC2_ParseAnchor(cur, frame);
        } else if (!strcasecmp(tag, "Texture")) {
            SC2_ParseTexture(cur, frame, -1);
        } else if (!strcasecmp(tag, "TextureType")) {
            int layer = 0;
            xmlGetAttrInt(cur, "layer", &layer);
            if (layer >= 0 && layer < SC2_MAX_TEXTURES) {
                LPCSTR val = SC2_XmlGetProp(cur, "val");
                if (val) {
                    SC2_Strncpyz(frame->textures[layer].texture_type, val, sizeof(frame->textures[0].texture_type));
                    SC2_XmlFree(val);
                }
            }
        } else if (!strcasecmp(tag, "StateCount")) {
            int layer = 0;
            xmlGetAttrInt(cur, "layer", &layer);
        } else if (!strcasecmp(tag, "LayerCount")) {
            int val = 0;
            if (xmlGetAttrInt(cur, "val", &val)) {
                if (val > frame->num_textures) frame->num_textures = val;
            }
        } else if (!strcasecmp(tag, "Visible")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                if (!strcasecmp(val, "true") || !strcasecmp(val, "1"))
                    frame->flags |= SC2_FRAME_VISIBLE;
                else
                    frame->flags &= ~SC2_FRAME_VISIBLE;
                frame->flags |= SC2_FRAME_HAS_VISIBLE;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "Alpha")) {
            FLOAT val;
            if (xmlGetAttrFloat(cur, "val", &val)) {
                frame->alpha = val;
                frame->flags |= SC2_FRAME_HAS_ALPHA;
            }
        } else if (!strcasecmp(tag, "Color")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                LPCSTR resolved = SC2_LayoutResolveConstant(val);
                frame->color = SC2_ParseColor(resolved ? resolved : val);
                frame->flags |= SC2_FRAME_HAS_COLOR;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "AcceptsMouse")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                if (!strcasecmp(val, "true") || !strcasecmp(val, "1"))
                    frame->flags |= SC2_FRAME_ACCEPTS_MOUSE;
                else
                    frame->flags &= ~SC2_FRAME_ACCEPTS_MOUSE;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "CollapseLayout")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                if (!strcasecmp(val, "true") || !strcasecmp(val, "1"))
                    frame->flags |= SC2_FRAME_COLLAPSE_LAYOUT;
                else
                    frame->flags &= ~SC2_FRAME_COLLAPSE_LAYOUT;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "HighlightOnHover")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                if (!strcasecmp(val, "true") || !strcasecmp(val, "1"))
                    frame->flags |= SC2_FRAME_HIGHLIGHT_ON_HOVER;
                else
                    frame->flags &= ~SC2_FRAME_HIGHLIGHT_ON_HOVER;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "HighlightOnFocus")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                if (!strcasecmp(val, "true") || !strcasecmp(val, "1"))
                    frame->flags |= SC2_FRAME_HIGHLIGHT_ON_FOCUS;
                else
                    frame->flags &= ~SC2_FRAME_HIGHLIGHT_ON_FOCUS;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "BatchImages")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                if (!strcasecmp(val, "true") || !strcasecmp(val, "1"))
                    frame->flags |= SC2_FRAME_BATCH_IMAGES;
                else
                    frame->flags &= ~SC2_FRAME_BATCH_IMAGES;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "BatchText")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                if (!strcasecmp(val, "true") || !strcasecmp(val, "1"))
                    frame->flags |= SC2_FRAME_BATCH_TEXT;
                else
                    frame->flags &= ~SC2_FRAME_BATCH_TEXT;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "DescFlags")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                if (!strcasecmp(val, "Internal"))
                    frame->flags |= SC2_FRAME_DESC_FLAGS_INTERNAL;
                else
                    frame->flags &= ~SC2_FRAME_DESC_FLAGS_INTERNAL;
                frame->flags |= SC2_FRAME_HAS_DESC_FLAGS;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "Model")) {
            SC2_ParseModel(cur, frame);
        } else if (!strcasecmp(tag, "Frame")) {
            if (frame->num_children < SC2_MAX_CHILDREN) {
                sc2Frame_t *child = SC2_AddTemplate();
                if (child) {
                    SC2_ParseFrameAttrs(cur, child);
                    SC2_ParseFrameChildren(cur, child);
                    child->parent = frame;
                    frame->children[frame->num_children++] = child;
                }
            }
        }
    }
}

static void SC2_ParseTopLevelFrame(void *node) {
    sc2Frame_t *frame = SC2_AddTemplate();
    if (!frame) return;
    SC2_ParseFrameAttrs(node, frame);
    SC2_ParseFrameChildren(node, frame);
}

static void SC2_ParseDescNode(void *node) {
    SC2_ResolveIncludes(node);

    for (xmlNode *cur = (xmlNode *)node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        if (!strcasecmp((const char *)cur->name, "Constant"))
            SC2_ParseConstant(cur);
    }

    int templates_before = sc2_layout.num_templates;
    for (xmlNode *cur = (xmlNode *)node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        if (!strcasecmp((const char *)cur->name, "Frame"))
            SC2_ParseTopLevelFrame(cur);
    }

    int templates_end = sc2_layout.num_templates;
    for (int i = templates_before; i < templates_end; i++) {
        sc2Frame_t *frame = &sc2_layout.templates[i];
        if (frame->template_path[0]) {
            sc2Frame_t *tmpl = SC2_ResolveTemplatePath(frame->template_path);
            if (tmpl) {
                SC2_ResolveTemplate(frame, tmpl);
            }
        }
    }
}

FRAMETYPE SC2_MapFrameType(sc2FrameType sc2_type) {
    switch (sc2_type) {
    case SC2_FRAMETYPE_FRAME:
    case SC2_FRAMETYPE_CONSOLE_PANEL:
    case SC2_FRAMETYPE_COMMAND_PANEL:
    case SC2_FRAMETYPE_INFO_PANEL:
    case SC2_FRAMETYPE_MINIMAP_PANEL:
    case SC2_FRAMETYPE_RESOURCE_PANEL:
    case SC2_FRAMETYPE_PORTRAIT_PANEL:
    case SC2_FRAMETYPE_GAME_UI:
    case SC2_FRAMETYPE_WORLD_PANEL:
    case SC2_FRAMETYPE_HERO_PANEL:
    case SC2_FRAMETYPE_INVENTORY_PANEL:
    case SC2_FRAMETYPE_CONTROL_GROUP_PANEL:
    case SC2_FRAMETYPE_ALLIANCE_PANEL:
    case SC2_FRAMETYPE_TEAM_RESOURCE_PANEL:
    case SC2_FRAMETYPE_LEADER_PANEL:
    case SC2_FRAMETYPE_OBSERVER_PANEL:
    case SC2_FRAMETYPE_REPLAY_PANEL:
    case SC2_FRAMETYPE_TALKER_PANEL:
    case SC2_FRAMETYPE_PURCHASE_PANEL:
    case SC2_FRAMETYPE_RESEARCH_PANEL:
    case SC2_FRAMETYPE_PLANET_PANEL:
    case SC2_FRAMETYPE_SYSTEM_ALERT_PANEL:
    case SC2_FRAMETYPE_TRIGGER_DIALOG:
    case SC2_FRAMETYPE_TRIGGER_WINDOW:
    case SC2_FRAMETYPE_CREDITS_PANEL:
    case SC2_FRAMETYPE_PAUSE_PANEL:
    case SC2_FRAMETYPE_CONVERSATION_PANEL:
    case SC2_FRAMETYPE_OBJECTIVE_PANEL:
    case SC2_FRAMETYPE_SUBTITLE_PANEL:
    case SC2_FRAMETYPE_TIME_PANEL:
    case SC2_FRAMETYPE_MENU_BAR:
    case SC2_FRAMETYPE_ALERT_PANEL:
    case SC2_FRAMETYPE_ALERT_DISPLAY:
    case SC2_FRAMETYPE_CHAT_BAR:
    case SC2_FRAMETYPE_RESOURCE_REQUEST_ALERT:
        return FT_FRAME;
    case SC2_FRAMETYPE_BUTTON:
    case SC2_FRAMETYPE_COMMAND_BUTTON:
    case SC2_FRAMETYPE_IDLE_BUTTON:
    case SC2_FRAMETYPE_AI_BUTTON:
    case SC2_FRAMETYPE_PYLON_BUTTON:
    case SC2_FRAMETYPE_LEROY_BUTTON:
        return FT_BUTTON;
    case SC2_FRAMETYPE_IMAGE:
        return FT_TEXTURE;
    case SC2_FRAMETYPE_MINIMAP:
        return FT_MINIMAP;
    case SC2_FRAMETYPE_MODEL:
        return FT_SPRITE;
    case SC2_FRAMETYPE_LABEL:
    case SC2_FRAMETYPE_COUNTDOWN_LABEL:
        return FT_TEXT;
    case SC2_FRAMETYPE_EDITBOX:
        return FT_EDITBOX;
    case SC2_FRAMETYPE_TOOLTIP:
    case SC2_FRAMETYPE_COMMAND_TOOLTIP:
    case SC2_FRAMETYPE_MINIMAP_PANEL_TOOLTIP:
        return FT_SIMPLEFRAME;
    case SC2_FRAMETYPE_MESSAGE_DISPLAY:
        return FT_TEXTAREA;
    default:
        return FT_FRAME;
    }
}

static LPCSTR SC2_ParseRelativeName(LPCSTR relative, LPCSTR parent_name) {
    if (!relative) return NULL;
    if (!strcasecmp(relative, "$parent")) return parent_name;
    if (!strcasecmp(relative, "$root")) return NULL;
    if (!strncasecmp(relative, "$parent/", 8)) return relative + 8;
    return relative;
}

static void SC2_ResolveAnchors(sc2Frame_t *src, sc2BaseFrame_t *dst) {
    if (src->flags & SC2_FRAME_HAS_WIDTH) dst->size.width = src->width;
    if (src->flags & SC2_FRAME_HAS_HEIGHT) dst->size.height = src->height;

    sc2Frame_t *parent = src->parent;
    LPCSTR parent_name = parent ? parent->name : NULL;

    for (int i = 0; i < src->num_anchors; i++) {
        sc2ParsedAnchor_t *a = &src->anchors[i];
        if (!(a->flags & SC2_ANCHOR_HAS)) continue;

        int point_idx;
        BOOL is_x;
        switch (a->side) {
            case SC2_SIDE_LEFT:   is_x = true;  point_idx = FPP_MIN; break;
            case SC2_SIDE_RIGHT:  is_x = true;  point_idx = FPP_MAX; break;
            case SC2_SIDE_TOP:    is_x = false; point_idx = FPP_MIN; break;
            case SC2_SIDE_BOTTOM: is_x = false; point_idx = FPP_MAX; break;
            default: continue;
        }

        int target_idx = (a->pos == SC2_POS_MIN) ? FPP_MIN :
                         (a->pos == SC2_POS_MID) ? FPP_MID : FPP_MAX;

        sc2BaseFramePoint_t *p = is_x ? &dst->points.x[point_idx] : &dst->points.y[point_idx];
        p->used = true;
        p->targetPos = (uiFramePointPos_t)target_idx;
        p->offset = is_x ? (FLOAT)a->offset : -(FLOAT)a->offset;

        LPCSTR resolved_name = SC2_ParseRelativeName(a->relative, parent_name);
        if (!resolved_name || !strcasecmp(resolved_name, parent_name)) {
            p->relative_index = dst->parent_index;
        } else if (!strcasecmp(a->relative, "$root")) {
            p->relative_index = 0;
        } else {
            p->relative_name = resolved_name;
        }
    }
}

static void SC2_ResolveNamedRelatives(void) {
    for (DWORD i = 0; i < (DWORD)sc2_layout.num_frames; i++) {
        sc2BaseFrame_t *dst = &sc2_layout.frames[i];
        for (int axis = 0; axis < 2; axis++) {
            sc2BaseFramePoint_t *pts = axis == 0 ? dst->points.x : dst->points.y;
            for (int j = 0; j < FPP_COUNT; j++) {
                LPCSTR look_name = pts[j].relative_name;
                if (!look_name) continue;
                for (DWORD m = 0; m < (DWORD)sc2_layout.num_frames; m++) {
                    if (sc2_layout.frames[m].name && !strcasecmp(sc2_layout.frames[m].name, look_name)) {
                        pts[j].relative_index = m;
                        break;
                    }
                }
                pts[j].relative_name = NULL;
            }
        }
    }
}

static void SC2_DrawImage(struct sc2BaseFrame_s *frame, LPCRECT rect) {
    LPRENDERER renderer = uiimport.GetRenderer();
    if (!renderer || !renderer->DrawImage) return;
    LPCTEXTURE tex = frame->image ? uiimport.GetTexture(frame->image) : NULL;
    if (!tex) return;
    static const RECT uv = { 0.0f, 0.0f, 1.0f, 1.0f };
    renderer->DrawImage(tex, rect, &uv, frame->color);
}

static void SC2_DrawText(struct sc2BaseFrame_s *frame, LPCRECT rect) {
    LPRENDERER renderer = uiimport.GetRenderer();
    if (!renderer || !renderer->DrawText || !frame->text || !*frame->text) return;
    LPCFONT font = uiimport.GetFont ? uiimport.GetFont(0) : NULL;
    if (!font) return;
    renderer->DrawText(&MAKE(drawText_t,
                             .font = font,
                             .text = frame->text,
                             .rect = *rect,
                             .color = frame->color,
                             .textWidth = rect->w,
                             .flags = 0,
                             .lineHeight = 1.0f));
}

static void SC2_DrawButton(struct sc2BaseFrame_s *frame, LPCRECT rect) {
    LPRENDERER renderer = uiimport.GetRenderer();
    if (!renderer || !renderer->DrawImage) return;
    LPCTEXTURE tex = frame->image ? uiimport.GetTexture(frame->image) : NULL;
    if (!tex) return;
    static const RECT uv = { 0.0f, 0.0f, 1.0f, 1.0f };
    renderer->DrawImageEx(&MAKE(drawImage_t,
                                .texture = tex,
                                .shader = SHADER_COMMANDBUTTON,
                                .alphamode = BLEND_MODE_ALPHAKEY,
                                .screen = *rect,
                                .uv = uv,
                                .color = frame->color));
}

static void SC2_FlattenFrame(sc2Frame_t *frame, int parent_index) {
    if (sc2_layout.num_frames >= SC2_MAX_FRAMES) return;

    int index = sc2_layout.num_frames++;
    sc2BaseFrame_t *dst = &sc2_layout.frames[index];
    memset(dst, 0, sizeof(*dst));

    dst->number = (DWORD)index;
    dst->type = SC2_MapFrameType(frame->type);
    dst->sc2_type = frame->type;
    dst->name = frame->name;
    dst->parent_index = (parent_index >= 0) ? (DWORD)parent_index : (DWORD)-1;

    switch (dst->type) {
        case FT_SPRITE: dst->on_draw = SC2_DrawImage;  break;
        case FT_BUTTON: dst->on_draw = SC2_DrawButton; break;
        case FT_TEXT:
            dst->on_draw = SC2_DrawText;
            if (uiimport.FontIndex)
                dst->label.font = (RESOURCE)uiimport.FontIndex("UI/Fonts/EurostileExt-Med.otf", 16);
            dst->label.textaligny = FONT_JUSTIFYMIDDLE;
            break;
        default:        dst->on_draw = NULL;            break;
    }
    dst->color = (frame->flags & SC2_FRAME_HAS_COLOR) ? frame->color : (COLOR32){255, 255, 255, 255};
    dst->alpha = (frame->flags & SC2_FRAME_HAS_ALPHA) ? frame->alpha : 1.0f;
    dst->ui_flags = 0;
    if (frame->flags & SC2_FRAME_HAS_VISIBLE) {
        if (!(frame->flags & SC2_FRAME_VISIBLE))
            dst->ui_flags |= SC2_UIFLAG_HIDDEN | SC2_UIFLAG_HIDDEN_IN_HIERARCHY;
    }

    SC2_ResolveAnchors(frame, dst);

    if (frame->num_textures > 0 && frame->textures[0].flags & SC2_TEX_HAS_TEXTURE)
        dst->image = uiimport.ImageIndex ? (DWORD)uiimport.ImageIndex(frame->textures[0].resource) : 0;

    for (int i = 0; i < frame->num_textures; i++) {
        sc2ParsedTexture_t *tex = &frame->textures[i];
        if (!(tex->flags & SC2_TEX_HAS_TEXTURE)) continue;
        if ((tex->flags & SC2_TEX_TILED) && dst->backdrop.bg == 0) {
            dst->backdrop.bg = 0;
            dst->backdrop.flags |= SC2_BACKDROP_TILE;
        }
        if (!strcasecmp(tex->texture_type, "Border") && dst->backdrop.edge == 0) {
            dst->backdrop.edge = 0;
        }
    }

    frame->resolved_frame = dst;

    for (int i = 0; i < frame->num_children; i++)
        SC2_FlattenFrame(frame->children[i], index);
}

void SC2_LayoutInit(void) {
    memset(&sc2_layout, 0, sizeof(sc2_layout));
    for (int i = 0; i < SC2_MAX_TEMPLATES; i++)
        sc2_layout.templates[i].resolved_frame = NULL;
}

void SC2_LayoutShutdown(void) {
    memset(&sc2_layout, 0, sizeof(sc2_layout));
}

BOOL SC2_LayoutParseFile(LPCSTR filename) {
    void *buf = NULL;
    int len = uiimport.FS_ReadFile(filename, &buf);
    if (len < 0 || !buf) {
        fprintf(stderr, "SC2_Layout: failed to load '%s'\n", filename);
        return false;
    }

    xmlDocPtr doc = xmlParseMemory(buf, len);
    uiimport.FS_FreeFile(buf);
    if (!doc) {
        fprintf(stderr, "SC2_Layout: failed to parse '%s'\n", filename);
        return false;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root || strcasecmp((const char *)root->name, "Desc")) {
        fprintf(stderr, "SC2_Layout: root element is not <Desc> in '%s'\n", filename);
        xmlFreeDoc(doc);
        return false;
    }

    SC2_ParseDescNode(root->children);
    xmlFreeDoc(doc);
    return true;
}

BOOL SC2_LayoutBuildMainMenu(void) {
    SC2_LayoutInit();
    static LPCSTR glue_files[] = {
        "UI/Layout/Common/StandardConstants.SC2Layout",
        "UI/Layout/Common/StandardTemplates.SC2Layout",
        "UI/Layout/Glue/GlueMainMenu.SC2Layout",
        NULL,
    };
    for (int i = 0; glue_files[i]; i++)
        if (!SC2_LayoutParseFile(glue_files[i]))
            fprintf(stderr, "SC2_Layout: failed to load glue file '%s'\n", glue_files[i]);
    return SC2_LayoutFlatten("GlueMainMenu");
}

BOOL SC2_LayoutBuildGameUI(void) {
    SC2_LayoutInit();
    static LPCSTR core_files[] = {
        "UI/Layout/Common/StandardConstants.SC2Layout",
        "UI/Layout/UI/GameButton.SC2Layout",
        "UI/Layout/Common/StandardTemplates.SC2Layout",
        "UI/Layout/Common/StandardTooltip.SC2Layout",
        "UI/Layout/Common/StandardDialog.SC2Layout",
        "UI/Layout/UI/ConsolePanel.SC2Layout",
        "UI/Layout/UI/CommandPanel.SC2Layout",
        "UI/Layout/UI/CommandButton.SC2Layout",
        "UI/Layout/UI/ResourcePanel.SC2Layout",
        "UI/Layout/UI/MinimapPanel.SC2Layout",
        "UI/Layout/UI/PortraitPanel.SC2Layout",
        "UI/Layout/UI/InfoPanel.SC2Layout",
        "UI/Layout/UI/MenuBar.SC2Layout",
        "UI/Layout/UI/ControlGroupPanel.SC2Layout",
        "UI/Layout/UI/AlertPanel.SC2Layout",
        "UI/Layout/UI/ObjectivePanel.SC2Layout",
        "UI/Layout/UI/TimePanel.SC2Layout",
        "UI/Layout/UI/ConversationPanel.SC2Layout",
        "UI/Layout/UI/SubtitlePanel.SC2Layout",
        "UI/Layout/UI/CashPanel.SC2Layout",
        "UI/Layout/UI/GameUI.SC2Layout",
        NULL,
    };
    for (int i = 0; core_files[i]; i++)
        if (!SC2_LayoutParseFile(core_files[i]))
            fprintf(stderr, "SC2_Layout: failed to load core file '%s'\n", core_files[i]);
    int resolve_end = sc2_layout.num_templates;
    for (int i = 0; i < resolve_end; i++) {
        sc2Frame_t *frame = &sc2_layout.templates[i];
        if (frame->template_path[0]) {
            sc2Frame_t *tmpl = SC2_ResolveTemplatePath(frame->template_path);
            if (tmpl) {
                SC2_ResolveTemplate(frame, tmpl);
                frame->template_path[0] = '\0';
            }
        }
    }
    return SC2_LayoutFlatten("GameUI");
}

BOOL SC2_LayoutFlatten(LPCSTR root_name) {
    sc2Frame_t *root = SC2_FindTemplate(root_name);
    if (!root) {
        fprintf(stderr, "SC2_Layout: '%s' template not found\n", root_name);
        return false;
    }
    sc2_layout.num_frames = 0;
    SC2_FlattenFrame(root, -1);
    SC2_ResolveNamedRelatives();
    fprintf(stderr, "SC2_Layout: built %d frames from %d templates, %d constants\n",
            sc2_layout.num_frames, sc2_layout.num_templates, sc2_layout.num_constants);
    return true;
}

sc2BaseFrame_t *SC2_LayoutGetFrames(DWORD *count) {
    if (count) *count = (DWORD)sc2_layout.num_frames;
    return sc2_layout.frames;
}

sc2Frame_t *SC2_LayoutFindTemplate(LPCSTR name) {
    return SC2_FindTemplate(name);
}

sc2BaseFrame_t *SC2_LayoutFindFrameByType(sc2FrameType type) {
    for (int i = 0; i < sc2_layout.num_templates; i++) {
        sc2Frame_t *tmpl = &sc2_layout.templates[i];
        if (tmpl->type == type && tmpl->resolved_frame)
            return tmpl->resolved_frame;
    }
    return NULL;
}

sc2BaseFrame_t *SC2_LayoutFindFrameByName(LPCSTR name) {
    for (int i = 0; i < sc2_layout.num_frames; i++)
        if (sc2_layout.frames[i].name && !strcasecmp(sc2_layout.frames[i].name, name))
            return &sc2_layout.frames[i];
    return NULL;
}

sc2BaseFrame_t *SC2_LayoutFindChildFrame(sc2BaseFrame_t *parent, LPCSTR name) {
    if (!parent || !name) return NULL;
    for (DWORD i = 0; i < (DWORD)sc2_layout.num_frames; i++) {
        if (sc2_layout.frames[i].parent_index == parent->number &&
            sc2_layout.frames[i].name && !strcasecmp(sc2_layout.frames[i].name, name))
            return &sc2_layout.frames[i];
    }
    return NULL;
}

int SC2_LayoutNumTemplates(void) {
    return sc2_layout.num_templates;
}

sc2Frame_t *SC2_LayoutGetTemplate(int index) {
    if (index < 0 || index >= sc2_layout.num_templates) return NULL;
    return &sc2_layout.templates[index];
}

void SC2_InitFrame(sc2Frame_t *frame, sc2FrameType type) {
    memset(frame, 0, sizeof(*frame));
    frame->type = type;
    frame->alpha = 1.0f;
    frame->color = (COLOR32){255, 255, 255, 255};
}

void SC2_SetSize(sc2Frame_t *frame, FLOAT width, FLOAT height) {
    frame->width = width;
    frame->height = height;
    frame->flags |= SC2_FRAME_HAS_WIDTH | SC2_FRAME_HAS_HEIGHT;
}

void SC2_SetHidden(sc2Frame_t *frame, BOOL value) {
    frame->flags |= SC2_FRAME_HAS_VISIBLE;
    if (value) frame->flags &= ~SC2_FRAME_VISIBLE;
    else frame->flags |= SC2_FRAME_VISIBLE;
}

void SC2_SetEnabled(sc2Frame_t *frame, BOOL enabled) {
    if (enabled) frame->flags |= SC2_FRAME_ACCEPTS_MOUSE;
    else frame->flags &= ~SC2_FRAME_ACCEPTS_MOUSE;
}

#endif /* stb_sc2layout_impl */
#endif /* STB_SC2LAYOUT_IMPLEMENTATION */
