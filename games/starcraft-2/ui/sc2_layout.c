/*
 * sc2_layout.c — SC2 .SC2Layout XML parser and frame builder.
 *
 * Parses StarCraft II UI layout files (.SC2Layout) into uiBaseFrame_t arrays.
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
 *          produce flat uiBaseFrame_t array for client rendering.
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
    name++; /* skip '#' */
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
    if (!frame->has_width && tmpl->has_width) {
        frame->width = tmpl->width;
        frame->has_width = true;
    }
    if (!frame->has_height && tmpl->has_height) {
        frame->height = tmpl->height;
        frame->has_height = true;
    }
    if (!frame->has_visible && tmpl->has_visible) {
        frame->visible = tmpl->visible;
        frame->has_visible = true;
    }
    if (!frame->has_color && tmpl->has_color) {
        frame->color = tmpl->color;
        frame->has_color = true;
    }
    if (!frame->has_alpha && tmpl->has_alpha) {
        frame->alpha = tmpl->alpha;
        frame->has_alpha = true;
    }
    if (!frame->accepts_mouse) frame->accepts_mouse = tmpl->accepts_mouse;
    if (!frame->collapse_layout) frame->collapse_layout = tmpl->collapse_layout;
    if (!frame->highlight_on_hover) frame->highlight_on_hover = tmpl->highlight_on_hover;
    if (!frame->highlight_on_focus) frame->highlight_on_focus = tmpl->highlight_on_focus;

    /* Inherit anchors if none set on this frame */
    if (frame->num_anchors == 0 && tmpl->num_anchors > 0) {
        for (int i = 0; i < tmpl->num_anchors; i++) {
            frame->anchors[i] = tmpl->anchors[i];
            frame->num_anchors++;
        }
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
    a->offset = 0;
    xmlGetAttrInt(node, "offset", (int *)&a->offset);
    if (relative) {
        SC2_Strncpyz(a->relative, relative, sizeof(a->relative));
        SC2_XmlFree(relative);
    } else {
        SC2_Strncpyz(a->relative, "$parent", sizeof(a->relative));
    }
    a->has_anchor = true;
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
        tex->has_texture = true;
    }
    tex->layer = layer;

    LPCSTR tiled = SC2_XmlGetProp(node,"tiled");
    if (tiled) {
        tex->tiled = (!strcasecmp(tiled, "true") || !strcasecmp(tiled, "1"));
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
        tex->has_texture = true;
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
            FLOAT val;
            if (xmlGetAttrFloat(cur, "val", &val)) {
                frame->width = val;
                frame->has_width = true;
            }
        } else if (!strcasecmp(tag, "Height")) {
            FLOAT val;
            if (xmlGetAttrFloat(cur, "val", &val)) {
                frame->height = val;
                frame->has_height = true;
            }
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
                frame->visible = (!strcasecmp(val, "true") || !strcasecmp(val, "1"));
                frame->has_visible = true;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "Alpha")) {
            FLOAT val;
            if (xmlGetAttrFloat(cur, "val", &val)) {
                frame->alpha = val;
                frame->has_alpha = true;
            }
        } else if (!strcasecmp(tag, "Color")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                frame->color = SC2_ParseColor(val);
                frame->has_color = true;
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "AcceptsMouse")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                frame->accepts_mouse = (!strcasecmp(val, "true") || !strcasecmp(val, "1"));
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "CollapseLayout")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                frame->collapse_layout = (!strcasecmp(val, "true") || !strcasecmp(val, "1"));
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "HighlightOnHover")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                frame->highlight_on_hover = (!strcasecmp(val, "true") || !strcasecmp(val, "1"));
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "HighlightOnFocus")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                frame->highlight_on_focus = (!strcasecmp(val, "true") || !strcasecmp(val, "1"));
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "BatchImages")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                frame->batch_images = (!strcasecmp(val, "true") || !strcasecmp(val, "1"));
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "BatchText")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                frame->batch_text = (!strcasecmp(val, "true") || !strcasecmp(val, "1"));
                SC2_XmlFree(val);
            }
        } else if (!strcasecmp(tag, "DescFlags")) {
            LPCSTR val = SC2_XmlGetProp(cur, "val");
            if (val) {
                frame->desc_flags_internal = (!strcasecmp(val, "Internal"));
                frame->has_desc_flags = true;
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

    /* Resolve template inheritance if specified */
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

    /* Pass 3: parse frame templates */
    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE) continue;
        if (!strcasecmp((const char *)cur->name, "Frame")) {
            SC2_ParseTopLevelFrame(cur);
        }
    }
}

/* ---------- Frame tree → flat array ---------- */

/* Map SC2 frame type to uiBaseFrame_t FRAMETYPE */
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

/* Resolve anchors: map SC2 side+pos to uiBaseFrame_t uiFramePoints_t */
static void SC2_ResolveAnchors(sc2Frame_t *src, uiBaseFrame_t *dst) {
    /* Default: all points at (0,0) */
    for (int i = 0; i < FPP_COUNT; i++) {
        dst->screen_rect.x = 0;
        dst->screen_rect.y = 0;
    }

    for (int i = 0; i < src->num_anchors; i++) {
        sc2Anchor_t *a = &src->anchors[i];
        if (!a->has_anchor) continue;

        /* Map SC2 side+pos to a frame point index.
         * SC2 uses separate anchors per axis:
         *   Top/Left → y/x at Min
         *   Bottom/Right → y/x at Max
         *   Left/Right → x at Min/Max
         *   Top/Bottom → y at Min/Max
         * pos=Min/Max maps to screen edge, pos=Mid maps to center. */
        (void)a;
    }

    /* For now, use explicit size-based layout: set screen_rect from width/height */
    if (src->has_width)
        dst->size.width = src->width;
    if (src->has_height)
        dst->size.height = src->height;
}

/* Recursively flatten frame tree into uiBaseFrame_t array */
static void SC2_FlattenFrame(sc2Frame_t *frame, int parent_index) {
    if (sc2_layout.num_frames >= SC2_MAX_FRAMES) return;

    int index = sc2_layout.num_frames++;
    uiBaseFrame_t *dst = &sc2_layout.frames[index];
    memset(dst, 0, sizeof(*dst));

    dst->number = (DWORD)index;
    dst->type = SC2_MapFrameType(frame->type);
    dst->parent_index = (parent_index >= 0) ? (DWORD)parent_index : (DWORD)-1;
    dst->color = frame->has_color ? frame->color : (COLOR32){255, 255, 255, 255};
    dst->alpha = frame->has_alpha ? frame->alpha : 1.0f;
    dst->hidden = frame->has_visible ? !frame->visible : false;
    dst->disabled = false;
    dst->ui_flags = 0;
    if (dst->hidden) dst->ui_flags |= UIFLAG_HIDDEN;

    if (frame->has_width) dst->size.width = frame->width;
    if (frame->has_height) dst->size.height = frame->height;

    /* Resolve first texture as primary image */
    if (frame->num_textures > 0 && frame->textures[0].has_texture) {
        /* Store texture reference index — resolved later by renderer */
        dst->image = 0; /* TODO: resolve @@UI/Name to image index */
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

    /* Find the GameUI root frame template */
    sc2Frame_t *gameui = SC2_FindTemplate("GameUI");
    if (!gameui) {
        fprintf(stderr, "SC2_Layout: 'GameUI' template not found\n");
        return false;
    }

    /* Flatten into frame array */
    sc2_layout.num_frames = 0;
    SC2_FlattenFrame(gameui, -1);

    fprintf(stderr, "SC2_Layout: built %d frames from %d templates, %d constants\n",
            sc2_layout.num_frames, sc2_layout.num_templates, sc2_layout.num_constants);

    return true;
}

uiBaseFrame_t *SC2_LayoutGetFrames(DWORD *count) {
    if (count) *count = (DWORD)sc2_layout.num_frames;
    return sc2_layout.frames;
}

sc2Frame_t *SC2_LayoutFindTemplate(LPCSTR name) {
    return SC2_FindTemplate(name);
}
