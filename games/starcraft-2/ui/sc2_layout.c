/*
 * sc2_layout.c — SC2 .SC2Layout XML parser and frame builder.
 *
 * Parses StarCraft II UI layout files (.SC2Layout) into sc2BaseFrame_t arrays.
 * Uses libxml2 for XML parsing (already linked for sc2_map.c).
 *
 * Format overview:
 *   <Desc>
 *     <Include path="UI/Layout/..."/>
 *     <Constant name="..." val="r,g,b"/>
 *     <Frame type="Button" name="..." template="Path/Template">
 *       <Width val="300"/>
 *       <Height val="101"/>
 *       <Anchor side="Top" relative="$parent" pos="Min" offset="0"/>
 *       <Texture val="@@UI/TextureName" layer="0"/>
 *       <Frame type="Image" name="ChildName">...</Frame>
 *     </Frame>
 *   </Desc>
 *
 * Phase 1: Parse XML into sc2Frame_t tree, resolve templates and constants,
 *          produce flat sc2BaseFrame_t array for client rendering.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "sc2_layout.h"
#include <libxml/parser.h>
#include <libxml/tree.h>

extern uiImport_t uiimport;

#ifndef CLAMP
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#endif

/* Safe string copy with null termination */
static void SC2_Strncpyz(char *dst, LPCSTR src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/* Wrapper for xmlGetProp that returns const char* without warnings */
static LPCSTR SC2_XmlGetProp(xmlNode *node, LPCSTR name) {
    xmlChar *val = xmlGetProp(node, (const xmlChar *)name);
    return (const char *)val;
}

/* Free a string returned by SC2_XmlGetProp */
static void SC2_XmlFree(LPCSTR s) {
    if (s) xmlFree((xmlChar *)s);
}

/* Global layout state */
static sc2Layout_t sc2_layout;

/* Frame type name → sc2FrameType mapping table */
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

/* Anchor side name → SC2Side mapping */
static struct { LPCSTR name; SC2Side side; } sc2_sides[] = {
    { "Top",    SC2_SIDE_TOP },
    { "Bottom", SC2_SIDE_BOTTOM },
    { "Left",   SC2_SIDE_LEFT },
    { "Right",  SC2_SIDE_RIGHT },
    { NULL, -1 },
};

/* Anchor position name → SC2Pos mapping */
static struct { LPCSTR name; SC2Pos pos; } sc2_positions[] = {
    { "Min", SC2_POS_MIN },
    { "Mid", SC2_POS_MID },
    { "Max", SC2_POS_MAX },
    { NULL, -1 },
};

/* ---------- Utility ---------- */

static sc2FrameType SC2_LookupFrameType(LPCSTR name) {
    for (int i = 0; sc2_frame_types[i].name; i++) {
        if (!strcasecmp(sc2_frame_types[i].name, name))
            return sc2_frame_types[i].type;
    }
    return SC2_FRAMETYPE_UNKNOWN;
}

static SC2Side SC2_LookupSide(LPCSTR name) {
    for (int i = 0; sc2_sides[i].name; i++) {
        if (!strcasecmp(sc2_sides[i].name, name))
            return sc2_sides[i].side;
    }
    return -1;
}

static SC2Pos SC2_LookupPos(LPCSTR name) {
    for (int i = 0; sc2_positions[i].name; i++) {
        if (!strcasecmp(sc2_positions[i].name, name))
            return sc2_positions[i].pos;
    }
    return -1;
}

static BOOL xmlGetAttrBool(xmlNode *node, LPCSTR name, BOOL *out) {
    LPCSTR val = SC2_XmlGetProp(node, name);
    if (!val) return false;
    *out = (!strcasecmp(val, "true") || !strcasecmp(val, "1") || !strcasecmp(val, "True"));
    SC2_XmlFree(val);
    return true;
}

static BOOL xmlGetAttrFloat(xmlNode *node, LPCSTR name, FLOAT *out) {
    LPCSTR val = SC2_XmlGetProp(node, name);
    if (!val) return false;
    *out = (FLOAT)atof(val);
    SC2_XmlFree(val);
    return true;
}

static BOOL xmlGetAttrInt(xmlNode *node, LPCSTR name, int *out) {
    LPCSTR val = SC2_XmlGetProp(node, name);
    if (!val) return false;
    *out = atoi(val);
    SC2_XmlFree(val);
    return true;
}

/* Parse "r,g,b" or "r,g,b,a" string into COLOR32 */
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

/* ---------- Template registry ---------- */

static sc2Frame_t *SC2_FindTemplate(LPCSTR name) {
    if (!name) return NULL;
    for (int i = 0; i < sc2_layout.num_templates; i++) {
        if (!strcasecmp(sc2_layout.templates[i].name, name))
            return &sc2_layout.templates[i];
    }
    return NULL;
}

static sc2Frame_t *SC2_AddTemplate(void) {
    if (sc2_layout.num_templates >= SC2_MAX_TEMPLATES) {
        fprintf(stderr, "SC2_Layout: template overflow\n");
        return NULL;
    }
    return &sc2_layout.templates[sc2_layout.num_templates++];
}

/* ---------- Constant registry ---------- */

static void SC2_AddConstant(LPCSTR name, LPCSTR val) {
    if (sc2_layout.num_constants >= SC2_MAX_CONSTANTS) return;
    sc2Constant_t *c = &sc2_layout.constants[sc2_layout.num_constants++];
    SC2_Strncpyz(c->name, name, sizeof(c->name));
    SC2_Strncpyz(c->val, val, sizeof(c->val));
}

LPCSTR SC2_LayoutResolveConstant(LPCSTR name) {
    if (!name || name[0] != '#') return name;
    while (*name == '#') name++; /* strip all leading '#' (SC2 uses ##Name) */
    for (int i = 0; i < sc2_layout.num_constants; i++) {
        if (!strcasecmp(sc2_layout.constants[i].name, name))
            return sc2_layout.constants[i].val;
    }
    return NULL;
}

/* ---------- XML parsing ---------- */

/* Forward declarations */
static void SC2_ParseFrameAttrs(xmlNode *node, sc2Frame_t *frame);
static void SC2_ParseFrameChildren(xmlNode *node, sc2Frame_t *frame);
static void SC2_ParseAnchor(xmlNode *node, sc2Frame_t *frame);
static void SC2_ParseTexture(xmlNode *node, sc2Frame_t *frame, int layer);
static void SC2_ParseModel(xmlNode *node, sc2Frame_t *frame);
static void SC2_ResolveTemplate(sc2Frame_t *frame, sc2Frame_t *tmpl);
static void SC2_ResolveIncludes(xmlNode *node);

/* Resolve template inheritance: clone template's children and properties */
static void SC2_ResolveTemplate(sc2Frame_t *frame, sc2Frame_t *tmpl) {
    if (!tmpl) return;

    /* Inherit properties that aren't explicitly set */
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

    /* Inherit anchors: template anchors are the base, frame's inline anchors
     * override the same side or are appended if new. This matches SC2 behavior
     * where the template provides base positioning and the frame can override
     * specific sides. */
    if (tmpl->num_anchors > 0) {
        /* First, copy template anchors as the base */
        int num = 0;
        sc2Anchor_t merged[SC2_MAX_ANCHORS];
        for (int i = 0; i < tmpl->num_anchors && num < SC2_MAX_ANCHORS; i++) {
            merged[num++] = tmpl->anchors[i];
        }
        /* Then, for each frame anchor, either override same-side or append */
        for (int i = 0; i < frame->num_anchors && num < SC2_MAX_ANCHORS; i++) {
            BOOL found = false;
            for (int j = 0; j < num; j++) {
                if (merged[j].side == frame->anchors[i].side) {
                    merged[j] = frame->anchors[i];
                    found = true;
                    break;
                }
            }
            if (!found) {
                merged[num++] = frame->anchors[i];
            }
        }
        memcpy(frame->anchors, merged, sizeof(merged));
        frame->num_anchors = num;
    }

    /* Inherit textures if none set */
    if (frame->num_textures == 0 && tmpl->num_textures > 0) {
        for (int i = 0; i < tmpl->num_textures; i++) {
            frame->textures[i] = tmpl->textures[i];
            frame->num_textures++;
        }
    }

    /* Clone template children (skip children that the frame already defines) */
    for (int i = 0; i < tmpl->num_children && frame->num_children < SC2_MAX_CHILDREN; i++) {
        sc2Frame_t *clone = SC2_AddTemplate();
        if (!clone) break;
        *clone = *tmpl->children[i];
        /* Clear child pointers to avoid double-free; re-parent later */
        clone->parent = frame;
        clone->resolved_index = -1;
        frame->children[frame->num_children++] = clone;
    }
}

/* Resolve template path: "CommandButton/CommandButtonTemplate" → lookup by basename */
static sc2Frame_t *SC2_ResolveTemplatePath(LPCSTR path) {
    if (!path) return NULL;

    /* Try exact match first */
    sc2Frame_t *t = SC2_FindTemplate(path);
    if (t) return t;

    /* Try basename — SC2 templates are referenced as "Dir/Name" but
     * registered with just "Name" */
    LPCSTR slash = strrchr(path, '/');
    if (slash) {
        t = SC2_FindTemplate(slash + 1);
        if (t) return t;
    }

    return NULL;
}

/* Parse <Include path="..."/> — load and parse referenced .SC2Layout file */
static void SC2_ParseInclude(xmlNode *node) {
    LPCSTR path = SC2_XmlGetProp(node,"path");
    if (!path) return;

    /* Check if already included */
    for (int i = 0; i < sc2_layout.num_includes; i++) {
        if (!strcasecmp(sc2_layout.included_files[i], path)) {
            SC2_XmlFree(path);
            return;
        }
    }

    /* Track inclusion */
    if (sc2_layout.num_includes < SC2_MAX_INCLUDES) {
        SC2_Strncpyz(sc2_layout.included_files[sc2_layout.num_includes++], path, MAX_PATHLEN);
    }

    /* Parse the included file */
    SC2_LayoutParseFile(path);
    SC2_XmlFree(path);
}

/* Process all <Include> elements first (top-level pass) */
static void SC2_ResolveIncludes(xmlNode *node) {
    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        if (!strcasecmp((const char *)cur->name, "Include")) {
            SC2_ParseInclude(cur);
        }
    }
}

/* Parse <Constant name="..." val="..."/> */
static void SC2_ParseConstant(xmlNode *node) {
    LPCSTR name = SC2_XmlGetProp(node,"name");
    LPCSTR val = SC2_XmlGetProp(node,"val");
    if (name && val) {
        SC2_AddConstant(name, val);
    }
    if (name) SC2_XmlFree(name);
    if (val) SC2_XmlFree(val);
}

/* Read an integer attribute, resolving ##constant references first */
static int SC2_ResolveAttrInt(xmlNode *node, LPCSTR attr, int default_val) {
    LPCSTR raw = SC2_XmlGetProp(node, attr);
    if (!raw) return default_val;
    LPCSTR resolved = SC2_LayoutResolveConstant(raw);
    int result = atoi(resolved ? resolved : raw);
    SC2_XmlFree(raw);
    return result;
}

/* Read a float attribute, resolving ##constant references first */
static FLOAT SC2_ResolveAttrFloat(xmlNode *node, LPCSTR attr, FLOAT default_val) {
    LPCSTR raw = SC2_XmlGetProp(node, attr);
    if (!raw) return default_val;
    LPCSTR resolved = SC2_LayoutResolveConstant(raw);
    FLOAT result = (FLOAT)atof(resolved ? resolved : raw);
    SC2_XmlFree(raw);
    return result;
}

/* Parse <Anchor side="..." relative="..." pos="..." offset="..."/> */
static void SC2_ParseAnchor(xmlNode *node, sc2Frame_t *frame) {
    if (frame->num_anchors >= SC2_MAX_ANCHORS) return;

    LPCSTR side_str = SC2_XmlGetProp(node,"side");
    LPCSTR pos_str = SC2_XmlGetProp(node,"pos");
    LPCSTR relative = SC2_XmlGetProp(node,"relative");

    if (!side_str || !pos_str) {
        if (side_str) SC2_XmlFree(side_str);
        if (pos_str) SC2_XmlFree(pos_str);
        if (relative) SC2_XmlFree(relative);
        return;
    }

    sc2Anchor_t *a = &frame->anchors[frame->num_anchors];
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

/* Parse <Texture val="..." layer="..."/> */
static void SC2_ParseTexture(xmlNode *node, sc2Frame_t *frame, int layer_override) {
    int layer = layer_override;
    xmlGetAttrInt(node, "layer", &layer);
    if (layer < 0 || layer >= SC2_MAX_TEXTURES) layer = 0;

    sc2Texture_t *tex = &frame->textures[layer];
    LPCSTR val = SC2_XmlGetProp(node,"val");
    if (val) {
        SC2_Strncpyz(tex->resource, val, sizeof(tex->resource));
        SC2_XmlFree(val);
        tex->flags |= SC2_TEX_HAS_TEXTURE;
    }
    tex->layer = layer;

    LPCSTR tiled = SC2_XmlGetProp(node,"tiled");
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

/* Parse <Model val="..." ...> child elements */
static void SC2_ParseModel(xmlNode *node, sc2Frame_t *frame) {
    LPCSTR val = SC2_XmlGetProp(node,"val");
    if (val) {
        /* Store model reference in the first texture slot */
        sc2Texture_t *tex = &frame->textures[0];
        SC2_Strncpyz(tex->resource, val, sizeof(tex->resource));
        tex->flags |= SC2_TEX_HAS_TEXTURE;
        frame->num_textures = 1;
        SC2_XmlFree(val);
    }
}

/* Parse a single frame's XML attributes */
static void SC2_ParseFrameAttrs(xmlNode *node, sc2Frame_t *frame) {
    /* type attribute */
    LPCSTR type_str = SC2_XmlGetProp(node,"type");
    if (type_str) {
        frame->type = SC2_LookupFrameType(type_str);
        SC2_XmlFree(type_str);
    } else {
        frame->type = SC2_FRAMETYPE_FRAME;
    }

    /* name attribute */
    LPCSTR name = SC2_XmlGetProp(node,"name");
    if (name) {
        SC2_Strncpyz(frame->name, name, sizeof(frame->name));
        SC2_XmlFree(name);
    }

    /* template attribute */
    LPCSTR tmpl = SC2_XmlGetProp(node,"template");
    if (tmpl) {
        SC2_Strncpyz(frame->template_path, tmpl, sizeof(frame->template_path));
        SC2_XmlFree(tmpl);
    }

    /* Image attribute (for tooltip border references) */
    LPCSTR image = SC2_XmlGetProp(node,"Image");
    if (image) {
        SC2_Strncpyz(frame->image_ref, image, sizeof(frame->image_ref));
        SC2_XmlFree(image);
    }

    frame->resolved_index = -1;
}

/* Parse child elements of a <Frame> node */
static void SC2_ParseFrameChildren(xmlNode *node, sc2Frame_t *frame) {
    for (xmlNode *cur = node->children; cur; cur = cur->next) {
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
            /* Used for texture state counts; store in texture layer */
            int layer = 0;
            xmlGetAttrInt(cur, "layer", &layer);
        } else if (!strcasecmp(tag, "LayerCount")) {
            int val = 0;
            if (xmlGetAttrInt(cur, "val", &val)) {
                if (val > frame->num_textures)
                    frame->num_textures = val;
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
        } else if (!strcasecmp(tag, "Insets")) {
            /* Parse insets for tooltips */
            /* TODO: store insets on frame */
        } else if (!strcasecmp(tag, "Style")) {
            /* Style references — not resolved yet */
        } else if (!strcasecmp(tag, "LayerColor")) {
            /* ##ConstantName color reference — resolve later */
        } else if (!strcasecmp(tag, "NormalImage") || !strcasecmp(tag, "HoverImage") ||
                   !strcasecmp(tag, "Label") || !strcasecmp(tag, "HitTestFrame")) {
            /* Button sub-references by name — skip, resolved at runtime */
        } else if (!strcasecmp(tag, "Model")) {
            SC2_ParseModel(cur, frame);
        } else if (!strcasecmp(tag, "Camera")) {
            /* Model camera — skip for now */
        } else if (!strcasecmp(tag, "Projection")) {
            /* Model projection — skip for now */
        } else if (!strcasecmp(tag, "Position") || !strcasecmp(tag, "Scale")) {
            /* Model transform — skip for now */
        } else if (!strcasecmp(tag, "Frame")) {
            /* Inline child frame */
            if (frame->num_children < SC2_MAX_CHILDREN) {
                sc2Frame_t *child = SC2_AddTemplate();
                if (child) {
                    SC2_ParseFrameAttrs(cur, child);
                    SC2_ParseFrameChildren(cur, child);
                    child->parent = frame;
                    frame->children[frame->num_children++] = child;
                }
            }
        } else if (!strcasecmp(tag, "RequiredDefines")) {
            /* Platform-specific define — skip for now */
        } else if (!strcasecmp(tag, "Options")) {
            /* Text options like "Ellipsis | NoWrapping" — skip for now */
        } else if (!strcasecmp(tag, "WriteOutText")) {
            /* Typewriter effect — skip for now */
        } else if (!strcasecmp(tag, "MaxWidth") || !strcasecmp(tag, "MinWidth")) {
            /* Tooltip sizing constraints — skip for now */
        } else if (!strcasecmp(tag, "AlternateTime") || !strcasecmp(tag, "MaxMessages") ||
                   !strcasecmp(tag, "MessageDuration") || !strcasecmp(tag, "FadeDuration") ||
                   !strcasecmp(tag, "TopToBottom")) {
            /* MessageDisplay properties — skip for now */
        } else if (!strcasecmp(tag, "ClickSound")) {
            /* Sound reference — skip for now */
        } else if (!strcasecmp(tag, "Shortcut")) {
            /* Keyboard shortcut — skip for now */
        } else if (!strcasecmp(tag, "Tooltip") || !strcasecmp(tag, "TooltipFrame")) {
            /* Tooltip reference — skip for now */
        } else if (!strcasecmp(tag, "File")) {
            /* Flash SWF file — skip for now */
        } else if (!strcasecmp(tag, "RenderType")) {
            /* HDR rendering — skip for now */
        } else if (!strcasecmp(tag, "RenderPriority")) {
            /* Draw order priority — skip for now */
        } else if (!strcasecmp(tag, "DescFlags")) {
            /* Frame flags — already handled above */
        }
        /* Unknown tags are silently ignored */
    }
}

/* Parse a top-level <Frame> as a template (no parent) */
static void SC2_ParseTopLevelFrame(xmlNode *node) {
    sc2Frame_t *frame = SC2_AddTemplate();
    if (!frame) return;

    SC2_ParseFrameAttrs(node, frame);
    SC2_ParseFrameChildren(node, frame);
    /* Template resolution deferred to second pass in SC2_ParseDescNode */
}

/* Parse XML document node (the <Desc> root) */
static void SC2_ParseDescNode(xmlNode *node) {
    /* Pass 1: resolve all includes first */
    SC2_ResolveIncludes(node);

    /* Pass 2: parse constants */
    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        if (!strcasecmp((const char *)cur->name, "Constant")) {
            SC2_ParseConstant(cur);
        }
    }

    /* Pass 3: parse frame templates (deferred template resolution) */
    int templates_before = sc2_layout.num_templates;
    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        if (!strcasecmp((const char *)cur->name, "Frame")) {
            SC2_ParseTopLevelFrame(cur);
        }
    }

    /* Pass 4: resolve template inheritance for all frames parsed in this file.
     * Deferring to after all frames are parsed ensures same-file templates
     * are fully populated before being used as bases. */
    for (int i = templates_before; i < sc2_layout.num_templates; i++) {
        sc2Frame_t *frame = &sc2_layout.templates[i];
        if (frame->template_path[0]) {
            sc2Frame_t *tmpl = SC2_ResolveTemplatePath(frame->template_path);
            if (tmpl) {
                SC2_ResolveTemplate(frame, tmpl);
            } else {
                fprintf(stderr, "SC2_Layout: template '%s' not found for frame '%s'\n",
                        frame->template_path, frame->name);
            }
        }
    }
}

/* ---------- Frame tree → flat array ---------- */

/* Map SC2 frame type to sc2BaseFrame_t FRAMETYPE */
static FRAMETYPE SC2_MapFrameType(sc2FrameType sc2_type) {
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

/* Resolve anchors: map SC2 side+pos to sc2BaseFrame_t points.x/y[FPP_*].
 *
 * SC2 anchor model: each anchor specifies which edge of the frame (side) and
 * which edge/position of the relative frame (pos) to attach to.
 *   side = Top, Bottom, Left, Right
 *   pos  = Min (top/left edge), Mid (center), Max (bottom/right edge)
 *
 * Mapping to sc2BaseFrame_t:
 *   Top+Min → y[FPP_MIN], Top+Mid → y[FPP_MID], Top+Max → y[FPP_MAX]
 *   Bottom+Min → y[FPP_MIN], Bottom+Mid → y[FPP_MID], Bottom+Max → y[FPP_MAX]
 *   Left+Min → x[FPP_MIN], Left+Mid → x[FPP_MID], Left+Max → x[FPP_MAX]
 *   Right+Min → x[FPP_MIN], Right+Mid → x[FPP_MID], Right+Max → x[FPP_MAX]
 *
 * SC2 offsets are in pixels with y-down positive. WC3 normalized space also
 * uses y-down, but the layout solver negates y offsets, so we negate here.
 *
 * Named relative frames ("$parent", "$root", or a frame name) are resolved
 * by index. $parent and $root are resolved immediately. Named frames are
 * resolved in SC2_ResolveNamedRelatives() after the full tree is flattened. */
static LPCSTR SC2_ParseRelativeName(LPCSTR relative, LPCSTR parent_name) {
    if (!relative) return NULL;
    if (!strcasecmp(relative, "$parent")) return parent_name;
    if (!strcasecmp(relative, "$root")) return NULL;
    /* $parent/ChildName → extract ChildName */
    if (!strncasecmp(relative, "$parent/", 8)) return relative + 8;
    return relative;
}

static void SC2_ResolveAnchors(sc2Frame_t *src, sc2BaseFrame_t *dst) {
    if (src->flags & SC2_FRAME_HAS_WIDTH) dst->size.width = src->width;
    if (src->flags & SC2_FRAME_HAS_HEIGHT) dst->size.height = src->height;

    sc2Frame_t *parent = NULL;
    if (dst->parent_index != (DWORD)-1) {
        for (int i = 0; i < sc2_layout.num_templates; i++) {
            if (sc2_layout.templates[i].resolved_index == (int)dst->parent_index) {
                parent = &sc2_layout.templates[i];
                break;
            }
        }
    }
    LPCSTR parent_name = parent ? parent->name : NULL;

    for (int i = 0; i < src->num_anchors; i++) {
        sc2Anchor_t *a = &src->anchors[i];
        if (!(a->flags & SC2_ANCHOR_HAS)) continue;

        /* side determines which point index on the axis */
        int point_idx;
        BOOL is_x;
        switch (a->side) {
            case SC2_SIDE_LEFT:   is_x = true;  point_idx = FPP_MIN; break;
            case SC2_SIDE_RIGHT:  is_x = true;  point_idx = FPP_MAX; break;
            case SC2_SIDE_TOP:    is_x = false; point_idx = FPP_MIN; break;
            case SC2_SIDE_BOTTOM: is_x = false; point_idx = FPP_MAX; break;
            default: continue;
        }

        /* pos determines target position on the parent */
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
            /* Named frame — mark for post-flatten resolution */
            p->relative_index = (DWORD)-2; /* sentinel: needs named resolution */
        }
    }
}

/* Resolve named relative references after the full frame tree is flattened.
 * Scans all frames for anchors with relative_index == -2 and looks up the
 * named frame in the flat array by matching sc2Frame_t name. */
static void SC2_ResolveNamedRelatives(void) {
    for (DWORD i = 0; i < (DWORD)sc2_layout.num_frames; i++) {
        sc2BaseFrame_t *dst = &sc2_layout.frames[i];
        for (int axis = 0; axis < 2; axis++) {
            sc2BaseFramePoint_t *pts = axis == 0 ? dst->points.x : dst->points.y;
            for (int j = 0; j < FPP_COUNT; j++) {
                if (pts[j].relative_index != (DWORD)-2) continue;
                sc2Frame_t *src = NULL;
                for (int k = 0; k < sc2_layout.num_templates; k++) {
                    if (sc2_layout.templates[k].resolved_index == (int)i) {
                        src = &sc2_layout.templates[k];
                        break;
                    }
                }
                if (!src) continue;
                /* Find the anchor that produced this sentinel */
                for (int k = 0; k < src->num_anchors; k++) {
                    sc2Anchor_t *a = &src->anchors[k];
                    if (!(a->flags & SC2_ANCHOR_HAS)) continue;
                    BOOL is_x_axis = (a->side == SC2_SIDE_LEFT || a->side == SC2_SIDE_RIGHT);
                    if ((axis == 0) != is_x_axis) continue;
                    int a_point = (a->side == SC2_SIDE_LEFT || a->side == SC2_SIDE_TOP) ? FPP_MIN :
                                  (a->side == SC2_SIDE_RIGHT || a->side == SC2_SIDE_BOTTOM) ? FPP_MAX : FPP_MID;
                    if (a_point != j) continue;
                    /* Extract the name to look up: $parent/ChildName → ChildName */
                    LPCSTR look_name = a->relative;
                    if (!strncasecmp(look_name, "$parent/", 8)) look_name += 8;
                    /* Look up named frame in flat array */
                    for (DWORD m = 0; m < (DWORD)sc2_layout.num_frames; m++) {
                        sc2Frame_t *f = NULL;
                        for (int n = 0; n < sc2_layout.num_templates; n++) {
                            if (sc2_layout.templates[n].resolved_index == (int)m) {
                                f = &sc2_layout.templates[n];
                                break;
                            }
                        }
                        if (f && !strcasecmp(f->name, look_name)) {
                            pts[j].relative_index = m;
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }
}

/* ---------- on_draw callbacks (called by client per frame) ---------- */

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

/* Recursively flatten frame tree into sc2BaseFrame_t array */
static void SC2_FlattenFrame(sc2Frame_t *frame, int parent_index) {
    if (sc2_layout.num_frames >= SC2_MAX_FRAMES) return;

    int index = sc2_layout.num_frames++;
    sc2BaseFrame_t *dst = &sc2_layout.frames[index];
    memset(dst, 0, sizeof(*dst));

    dst->number = (DWORD)index;
    dst->type = SC2_MapFrameType(frame->type);
    dst->parent_index = (parent_index >= 0) ? (DWORD)parent_index : (DWORD)-1;

    switch (dst->type) {
        case FT_SPRITE: dst->on_draw = SC2_DrawImage;  break;
        case FT_BUTTON: dst->on_draw = SC2_DrawButton; break;
        case FT_TEXT:   dst->on_draw = SC2_DrawText;   break;
        default:        dst->on_draw = NULL;            break;
    }
    dst->color = (frame->flags & SC2_FRAME_HAS_COLOR) ? frame->color : (COLOR32){255, 255, 255, 255};
    dst->alpha = (frame->flags & SC2_FRAME_HAS_ALPHA) ? frame->alpha : 1.0f;
    dst->ui_flags = 0;
    if (frame->flags & SC2_FRAME_HAS_VISIBLE) {
        if (!(frame->flags & SC2_FRAME_VISIBLE))
            dst->ui_flags |= SC2_UIFLAG_HIDDEN | SC2_UIFLAG_HIDDEN_IN_HIERARCHY;
    }

    /* Resolve anchor points */
    SC2_ResolveAnchors(frame, dst);

    /* Resolve first texture as primary image */
    if (frame->num_textures > 0 && frame->textures[0].flags & SC2_TEX_HAS_TEXTURE) {
        /* Store raw reference hash — resolved at render time */
        dst->image = 0; /* TODO: resolve @@UI/Name to image index */
    }

    /* Backdrop: first tiled texture as background, border textures as edge */
    for (int i = 0; i < frame->num_textures; i++) {
        sc2Texture_t *tex = &frame->textures[i];
        if (!(tex->flags & SC2_TEX_HAS_TEXTURE)) continue;
        if ((tex->flags & SC2_TEX_TILED) && dst->backdrop.bg == 0) {
            dst->backdrop.bg = 0; /* TODO: resolve texture */
            dst->backdrop.flags |= SC2_BACKDROP_TILE;
        }
        if (!strcasecmp(tex->texture_type, "Border") && dst->backdrop.edge == 0) {
            dst->backdrop.edge = 0; /* TODO: resolve texture */
        }
    }

    frame->resolved_index = index;

    /* Recurse children */
    for (int i = 0; i < frame->num_children; i++) {
        SC2_FlattenFrame(frame->children[i], index);
    }
}

/* ---------- Public API ---------- */

void SC2_LayoutInit(void) {
    memset(&sc2_layout, 0, sizeof(sc2_layout));
    for (int i = 0; i < SC2_MAX_TEMPLATES; i++) {
        sc2_layout.templates[i].resolved_index = -1;
    }
}

void SC2_LayoutShutdown(void) {
    memset(&sc2_layout, 0, sizeof(sc2_layout));
}

BOOL SC2_LayoutParseFile(LPCSTR filename) {
    /* Read file from MPQ via client callbacks */
    void *buf = NULL;
    int len = uiimport.FS_ReadFile(filename, &buf);
    if (len < 0 || !buf) {
        fprintf(stderr, "SC2_Layout: failed to load '%s'\n", filename);
        return false;
    }

    /* Parse XML */
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

/* Build main menu layout from glue screen layout files */
BOOL SC2_LayoutBuildMainMenu(void) {
    SC2_LayoutInit();

    static LPCSTR glue_files[] = {
        "UI/Layout/Common/StandardConstants.SC2Layout",
        "UI/Layout/Common/StandardTemplates.SC2Layout",
        "UI/Layout/Glue/GlueMainMenu.SC2Layout",
        NULL,
    };

    for (int i = 0; glue_files[i]; i++) {
        if (!SC2_LayoutParseFile(glue_files[i])) {
            fprintf(stderr, "SC2_Layout: failed to load glue file '%s'\n", glue_files[i]);
        }
    }

    return SC2_LayoutFlatten("GlueMainMenu");
}

/* Build the main game UI from known layout files */
BOOL SC2_LayoutBuildGameUI(void) {
    SC2_LayoutInit();

    /* Load core layouts — order matters: templates before consumers */
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
        "UI/Layout/UI/GameUI.SC2Layout",
        NULL,
    };

    for (int i = 0; core_files[i]; i++) {
        if (!SC2_LayoutParseFile(core_files[i])) {
            fprintf(stderr, "SC2_Layout: failed to load core file '%s'\n", core_files[i]);
        }
    }

    return SC2_LayoutFlatten("GameUI");
}

/* Flatten parsed templates into the frame array for client consumption */
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
        if (tmpl->type == type && tmpl->resolved_index >= 0)
            return &sc2_layout.frames[tmpl->resolved_index];
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
