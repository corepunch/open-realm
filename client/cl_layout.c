#include <limits.h>

#include "client.h"

#define PLAYERSTATE_RESOURCE_FOOD_CAP 4
#define PLAYERSTATE_RESOURCE_FOOD_USED 5

static UIFRAME frames[MAX_LAYOUT_OBJECTS];
static uint32_t num_frames = 0;
static uint32_t layout_runtime_layer = MAX_LAYOUT_LAYERS;

struct {
    rect_t rect;
    bool calculated;
} runtimes[MAX_LAYOUT_OBJECTS];

/* Server-authored layers anchor to the canvas HUD root: the whole scene under stretch/expand policies, the
 * centered authored 4:3 area under UI_CANVAS_EXPAND_CENTER.  World-hover replaces this root with a projected
 * point and UIFLAG_EXTEND_WIDESCREEN_X frames reach the full scene regardless of the root. */
rect_t SCR_LayoutSceneRect(void) { return CL_Canvas()->root; }

VECTOR2 get_x(rect_t const * rect) {
    return (VECTOR2) { rect->x, rect->x + rect->w };
}

VECTOR2 get_y(rect_t const * rect) {
    return (VECTOR2) { rect->y, rect->y + rect->h };
}

VECTOR2 SCR_GetAxisBounds(rect_t const * rect, bool is_x_axis) {
    return is_x_axis ? get_x(rect) : get_y(rect);
}

float SCR_NormalizeAnchorOffset(uiFramePoint_t const *p, bool is_x_axis) {
    int16_t offset = is_x_axis ? p->offset : -p->offset;
    return offset / UI_FRAMEPOINT_SCALE;
}

rect_t const * SCR_LayoutRectByNumber(LPCUIFRAME context, uint32_t number) {
    if (number == UI_PARENT) {
        return SCR_LayoutRect(frames+context->parent);
    } else {
        return SCR_LayoutRect(frames+number);
    }
}

float SCR_GetAnchor(LPCUIFRAME f,
                    uiFramePoint_t const *p,
                    VECTOR2 (*get)(rect_t const *))
{
    bool const is_x_axis = (get == get_x);
    VECTOR2 b = SCR_GetAxisBounds(SCR_LayoutRectByNumber(f, p->relativeTo), is_x_axis);
    float offset = SCR_NormalizeAnchorOffset(p, is_x_axis);
    if (p->targetPos == FPP_MID) {
        return (b.x + b.y) / 2 + offset;
    } else if (p->targetPos == FPP_MAX) {
        return b.y + offset;
    } else {
        return b.x + offset;
    }
}

VECTOR2 SCR_SolveAxisPosition(LPCUIFRAME frame,
                              uiFramePoints_t const points,
                              float width,
                              bool is_x_axis,
                              bool assigned_size)
{
    uiFramePoint_t const *pmin = points + FPP_MIN;
    uiFramePoint_t const *pmid = points + FPP_MID;
    uiFramePoint_t const *pmax = points + FPP_MAX;
    VECTOR2 (*get)(rect_t const *) = is_x_axis ? get_x : get_y;

    /* Warcraft preserves an authored Width/Height when both opposing anchors
     * exist.  Horizontal layout is left/min anchored; vertical layout is
     * bottom/max anchored (OpenRealm stores UI Y in top-left coordinates).
     * Only an auto-sized axis stretches between min and max. */
    if (assigned_size && pmin->used && pmax->used) {
        if (is_x_axis) {
            return (VECTOR2) {
                SCR_GetAnchor(frame, pmin, get),
                width,
            };
        }
        return (VECTOR2) {
            SCR_GetAnchor(frame, pmax, get) - width,
            width,
        };
    }

    if (pmid->used) {
        return (VECTOR2) {
            SCR_GetAnchor(frame, pmid, get) - width / 2,
            width,
        };
    } else if (pmin->used && pmax->used) {
        float anchor_min = SCR_GetAnchor(frame, pmin, get);
        float anchor_max = SCR_GetAnchor(frame, pmax, get);
        return (VECTOR2) {
            anchor_min,
            anchor_max - anchor_min,
        };
    } else if (pmax->used) {
        return (VECTOR2) {
            SCR_GetAnchor(frame, pmax, get) - width,
            width,
        };
    } else {
        return (VECTOR2) {
            SCR_GetAnchor(frame, pmin, get),
            width,
        };
    }
}

VECTOR2 get_position(LPCUIFRAME frame,
                     uiFramePoints_t const p,
                     float width,
                     VECTOR2 (*get)(rect_t const *),
                     bool assigned_size)
{
    return SCR_SolveAxisPosition(frame, p, width, get == get_x, assigned_size);
}

static VECTOR2 SCR_MeasureSizeToContent(LPCUIFRAME f, float avl) {
    uiNameTag_t const *t = f->buffer.data;
    drawText_t d = SCR_GetDrawText(f, avl, SCR_GetStringValue(f), &t->text);
    VECTOR2 s = re.GetTextSize(&d);
    return (VECTOR2){ MAX(t->min_width, s.x + t->padding_x * 2), s.y + t->padding_y * 2 };
}

/* Context bindings read only recipient-filtered snapshot state already present on the client. */
LPCENTITYSTATE SCR_LayoutContextEntity(void) {
    LPCENTITYSTATE ent;

    if (!cl.hover_entity || cl.hover_entity >= MAX_CLIENT_ENTITIES) return NULL;
    ent = &cl.ents[cl.hover_entity].current;
    if (!CL_EntityAllowsWorldHover(ent)) return NULL;
    return ent;
}

bool SCR_LayoutEntityContextActive(void) {
    return layout_runtime_layer == LAYER_WORLD_HOVER;
}

bool SCR_LayoutContextValue(uint32_t stat, float * value) {
    LPCENTITYSTATE ent;

    if (!value) return false;
    if (stat == UI_STAT_LOADING_PROGRESS) {
        *value = cl.loading_progress;
        return true;
    }
    if (stat == UI_STAT_SELECTION_TIMED_STATUS) {
        *value = cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS] / (float)USHRT_MAX;
        return true;
    }

    if (!SCR_LayoutEntityContextActive()) return false;
    ent = SCR_LayoutContextEntity();
    if (!ent) return false;
    switch (stat) {
        case UI_STAT_CONTEXT_HEALTH:
            if (!(ent->flags & EF_HOVER_HEALTH)) return false;
            *value = ent->stats[ENT_HEALTH] / 255.0f;
            break;
        case UI_STAT_CONTEXT_MANA:
            if (!(ent->flags & EF_HOVER_MANA)) return false;
            *value = ent->stats[ENT_MANA] / 255.0f;
            break;
        default:
            return false;
    }
    return true;
}

/* Context-bound presentation occupies no layout space when that capability is absent.
 * Keep zero mana visible when the mana capability exists, and keep segmented cargo
 * visible for an empty holder as long as its authored capacity is nonzero. */
bool SCR_LayoutContextFrameVisible(LPCUIFRAME frame) {
    LPCENTITYSTATE ent;
    float value;

    if (!frame) return false;
    if (!SCR_LayoutEntityContextActive()) return true;
    if (frame->stat == UI_STAT_CONTEXT_NAME) {
        ent = SCR_LayoutContextEntity();
        return ent && ent->name;
    }
    if (frame->stat == UI_STAT_CONTEXT_HEALTH)
        return SCR_LayoutContextValue(frame->stat, &value) && value > 0.0f;
    if (frame->stat == UI_STAT_CONTEXT_MANA)
        return SCR_LayoutContextValue(frame->stat, &value);
    if (frame->flags.type != FT_SEGMENTED_STATUSBAR) return true;

    ent = SCR_LayoutContextEntity();
    return ent && frame->stat < ENT_STAT_COUNT && EntityCargoCapacity(ent->stats[frame->stat]) > 0;
}

cstring_t SCR_GetStringValue(LPCUIFRAME frame) {
    static char text[1024] = { 0 };
    cstring_t edit_text = CL_WindowEditTextValue(frame ? frame->number : 0);

    if (edit_text) return edit_text;
    if (frame->stat == UI_STAT_SELECTION_HEALTH_TEXT) {
        snprintf(text, sizeof(text), "%u / %u",
                 (unsigned)cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_HEALTH],
                 (unsigned)cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_MAX_HEALTH]);
        return text;
    } else if (frame->stat == UI_STAT_SELECTION_MANA_TEXT) {
        uint32_t max_mana = cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_MAX_MANA];
        if (max_mana) {
            snprintf(text, sizeof(text), "%u / %u",
                     (unsigned)cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_MANA],
                     (unsigned)max_mana);
        } else {
            text[0] = '\0';
        }
        return text;
    } else if (SCR_LayoutEntityContextActive() && frame->stat == UI_STAT_CONTEXT_NAME) {
        LPCENTITYSTATE ent = SCR_LayoutContextEntity();
        cstring_t name;
        uint32_t ni, cs_index;

        if (!ent || !ent->name) { text[0] = '\0'; return text; }
        ni = ent->name - 1;
        cs_index = CS_GENERAL + (ni >> 4);
        if (cs_index >= MAX_CONFIGSTRINGS) { text[0] = '\0'; return text; }
        name = cl.configstrings[cs_index] + (ni & 0xF) * ENT_NAME_SLOT_SIZE;
        /* The server offsets present resource values by one so zero remains a
         * distinct, valid depleted-mine value instead of meaning absent. */
        if (ent->hover_value && frame->text && *frame->text) {
            snprintf(text, sizeof(text), "%.*s\n%s %u", ENT_NAME_SLOT_SIZE - 1, name,
                     frame->text, (unsigned)(ent->hover_value - 1));
            return text;
        }
        return name;
    } else if (frame->stat >= MAX_STATS && frame->stat < MAX_STATS + PLAYERTEXT_COUNT) {
        if (cl.playerstate.texts[frame->stat - MAX_STATS]) {
            strlcpy(text, cl.playerstate.texts[frame->stat - MAX_STATS], sizeof(text));
        } else {
            memset(text, 0, sizeof(text));
        }
    } else if (frame->stat == PLAYERSTATE_RESOURCE_FOOD_USED) {
        uint32_t food_used = cl.playerstate.stats[PLAYERSTATE_RESOURCE_FOOD_USED];
        uint32_t food_made = cl.playerstate.stats[PLAYERSTATE_RESOURCE_FOOD_CAP];
        uint32_t food_ceiling = cl.playerstate.stats[PLAYERSTATE_FOOD_CAP_CEILING];
        if (food_ceiling) food_made = MIN(food_made, food_ceiling);
        if (food_made) snprintf(text, sizeof(text), "%d/%d", food_used, food_made);
        else snprintf(text, sizeof(text), "%d", food_used);
    } else if (frame->stat > 0) {
        if (frame->stat < MAX_STATS) snprintf(text, sizeof(text), "%d", cl.playerstate.stats[frame->stat]);
        else text[0] = '\0';
    } else if (frame->text) {
        return frame->text;
    } else {
        text[0] = '\0';
    }
    return text;
}

static cstring_t SCR_GetTimeOfDayValue(LPCUIFRAME frame) {
    static char text[16];
    uint32_t day_minutes, total_minutes;
    uint64_t scaled;
    float day_hours;

    if (!frame || frame->stat != UI_PLAYERSTAT_ENV_PHASE) return "";
    day_hours = frame->value > 0.0f ? frame->value : 24.0f;
    day_minutes = (uint32_t)(day_hours * 60.0f + 0.5f);
    if (!day_minutes) day_minutes = 24u * 60u;

    /* The server replicates normalized day phase in a uint16_t. Reconstruct the
     * displayed game clock from that same phase; frame.value carries DayHours
     * so non-default gameplay constants stay coherent without layout resends. */
    scaled = (uint64_t)cl.playerstate.stats[UI_PLAYERSTAT_ENV_PHASE] * day_minutes;
    total_minutes = (uint32_t)((scaled + UINT16_MAX / 2u) / UINT16_MAX);
    total_minutes %= day_minutes;
    snprintf(text, sizeof(text), "%02u:%02u",
             (unsigned)(total_minutes / 60u), (unsigned)(total_minutes % 60u));
    return text;
}

cstring_t SCR_GetTooltipText(LPCUIFRAME frame) {
    static char text[2048];
    cstring_t src, token, value, suffix;
    size_t prefix;

    if (!frame || !frame->tooltip) return NULL;
    src = frame->tooltip;
    token = strstr(src, "{time}");
    if (token) {
        value = SCR_GetTimeOfDayValue(frame);
        suffix = token + strlen("{time}");
    } else {
        token = strstr(src, "{value}");
        if (!token) return src;
        value = SCR_GetStringValue(frame);
        suffix = token + strlen("{value}");
    }

    prefix = (size_t)(token - src);
    snprintf(text, sizeof(text), "%.*s%s%s", (int)prefix, src,
             value ? value : "", suffix);
    return text;
}

drawText_t SCR_GetDrawText(LPCUIFRAME frame,
                      float avl_width,
                      cstring_t text,
                      uiLabel_t const *label)
{
    LPCFONT font = cl.fonts[label->font];
    COLOR32 color = frame->color;

    /* FDF edit boxes own their text presentation. The STRING/TEXT child is
     * the editable value carrier, but its standalone label style may be empty
     * or transparent because retail rendering takes the font/text color from
     * the parent edit control. Mirror that contract for transient windows. */
    if (frame->parent < SCR_NumFrames()) {
        LPCUIFRAME parent = SCR_Frame(frame->parent);
        if (parent && (parent->flags.type == FT_EDITBOX ||
                       parent->flags.type == FT_GLUEEDITBOX ||
                       parent->flags.type == FT_SLASHCHATBOX) &&
            parent->buffer.data && parent->buffer.size >= sizeof(uiEditBox_t)) {
            uiEditBox_t const *edit = parent->buffer.data;
            if (edit->font) font = cl.fonts[edit->font];
            if (edit->textColor.a) color = edit->textColor;
        }
    }

    return MAKE(drawText_t,
                .font = font,
                .text = text,
                .color = color,
                .halign = label->textalignx,
                .valign = label->textaligny,
                .icons = cl.pics,
                .lineHeight = 1.33,
                .textWidth = avl_width);
}

rect_t const * SCR_LayoutRect(LPCUIFRAME frame) {
    bool const assigned_width = frame->size.width > 0;
    bool const assigned_height = frame->size.height > 0;
    if (runtimes[frame->number].calculated) {
        return &runtimes[frame->number].rect;
    } else {
        runtimes[frame->number].calculated = true; // done here to avoid recursion
    }
    if (!SCR_LayoutContextFrameVisible(frame)) {
        VECTOR2 const rect[] = {
            get_position(frame, frame->points.x, 0.0f, get_x, assigned_width),
            get_position(frame, frame->points.y, 0.0f, get_y, assigned_height),
        };
        runtimes[frame->number].rect = MAKE(rect_t, rect[0].x, rect[1].x, 0.0f, 0.0f);
        return &runtimes[frame->number].rect;
    }
    VECTOR2 elemsize = {0};
    float avl_space = runtimes[0].rect.w;
    drawText_t drawtext = {0};
    switch (frame->flags.type) {
        case FT_FRAME:
        case FT_SIMPLEFRAME:
            if ((frame->flagsvalue & UIFLAG_SIZE_TO_CONTENT) &&
                frame->buffer.data && frame->buffer.size >= sizeof(uiNameTag_t))
                elemsize = SCR_MeasureSizeToContent(frame, avl_space);
            break;
        case FT_STRING:
        case FT_TEXT: {
            uiLabel_t const *label = frame->buffer.data;
            if (frame->size.width > 0) {
                avl_space = frame->size.width;
            }
            drawtext = SCR_GetDrawText(frame, avl_space, SCR_GetStringValue(frame), label);
            elemsize = re.GetTextSize(&drawtext);
            if (frame->size.width == 0 && frame->textLength > 0) {
                drawText_t space = SCR_GetDrawText(frame, avl_space, " ", label);
                VECTOR2 const space_size = re.GetTextSize(&space);
                elemsize.x = (float)frame->textLength * space_size.x;
            }
            break;
        }
        case FT_NAMETAG: {
            uiNameTag_t const *tag = frame->buffer.data;
            if (frame->flagsvalue & UIFLAG_SIZE_TO_CONTENT)
                elemsize = SCR_MeasureSizeToContent(frame, avl_space);
            else {
                drawtext = SCR_GetDrawText(frame, avl_space, SCR_GetStringValue(frame), &tag->text);
                elemsize = re.GetTextSize(&drawtext);
            }
            break;
        }
        case FT_TEXTURE:
        case FT_SIMPLESTATUSBAR:
        case FT_SEGMENTED_STATUSBAR: {
            /* NormalImage/HoverImage semantics: when the frame has no explicit
               size AND no anchors on either axis, it fills the parent rect
               completely (SC2 button image fill-parent behaviour). */
            bool has_x_anchor = frame->points.x[FPP_MIN].used || frame->points.x[FPP_MID].used || frame->points.x[FPP_MAX].used;
            bool has_y_anchor = frame->points.y[FPP_MIN].used || frame->points.y[FPP_MID].used || frame->points.y[FPP_MAX].used;
            bool no_explicit_size = frame->size.width == 0 && frame->size.height == 0;
            if (no_explicit_size && !has_x_anchor && !has_y_anchor) {
                /* Fill parent: copy parent rect directly. */
                rect_t const * pr = SCR_LayoutRect(frames + frame->parent);
                runtimes[frame->number].rect = *pr;
                return &runtimes[frame->number].rect;
            }
            if (frame->size.width > 0 && frame->size.height > 0) {
                elemsize.x = frame->size.width;
                elemsize.y = frame->size.height;
            } else {
                rect_t const * pr = SCR_LayoutRect(frames + frame->parent);
                elemsize.x = pr->w;
                elemsize.y = pr->h;
            }
            break;
        }
        default:
            break;
    }
    /* Measured size is ignored on a fully min+max-anchored axis; the frame stretches. */
    if (frame->size.width == 0 && !(frame->points.x[FPP_MIN].used && frame->points.x[FPP_MAX].used)) {
        ((LPUIFRAME )frame)->size.width = elemsize.x;
    }
    if (frame->size.height == 0 && !(frame->points.y[FPP_MIN].used && frame->points.y[FPP_MAX].used)) {
        ((LPUIFRAME )frame)->size.height = elemsize.y;
    }
    VECTOR2 const rect[] = {
        get_position(frame, frame->points.x, frame->size.width, get_x, assigned_width),
        get_position(frame, frame->points.y, frame->size.height, get_y, assigned_height),
    };
    runtimes[frame->number].rect = (rect_t) {
        .x = rect[0].x,
        .y = rect[1].x,
        .w = rect[0].y,
        .h = rect[1].y,
    };
    if (frame->flagsvalue & UIFLAG_EXTEND_WIDESCREEN_X) {
        runtimes[frame->number].rect.x = 0.0f;
        runtimes[frame->number].rect.w = SCR_UICanvasWidth();
    }
    return &runtimes[frame->number].rect;
}

/* Resolve the y-coordinate of frame 'idx' relative to a pmax-only container's
 * implicit top (which we treat as 0).  Follows pmin_y anchor chains, resolving
 * FPP_MAX target to (ref_y + ref_h).  Used only by SCR_InferContainerHeights
 * before runtimes[] are populated — never reads runtimes[]. */
static float scr_frame_abs_y(uint32_t idx) {
    if (idx == 0 || idx >= num_frames) return 0;
    LPCUIFRAME f = &frames[idx];
    uiFramePoint_t const *pmin_y = &f->points.y[FPP_MIN];
    if (!pmin_y->used) return 0;
    uint32_t rel = pmin_y->relativeTo;
    float parent_y = (rel == UI_PARENT) ? scr_frame_abs_y(f->parent) :
                     (rel < num_frames) ? scr_frame_abs_y(rel) : 0;
    float off = -((float)pmin_y->offset / UI_FRAMEPOINT_SCALE);
    if (pmin_y->targetPos == FPP_MAX) {
        float parent_h = (rel == UI_PARENT) ? frames[f->parent].size.height :
                         (rel < num_frames) ? frames[rel].size.height : 0;
        return parent_y + parent_h + off;
    }
    return parent_y + off;
}

/* SC2 panels (CommandPanel, InfoPanel, etc.) often have only a Bottom anchor and
 * no explicit Height.  After wire parsing, for any FT_FRAME with size.height==0
 * and only pmax_y set, infer height from the max y-extent of all descendants. */
static void SCR_InferContainerHeights(void) {
    for (uint32_t p = num_frames; p-- > 1; ) {
        LPCUIFRAME f = &frames[p];
        if (f->size.height > 0) continue;
        if (f->flags.type != FT_FRAME) continue;
        if (f->points.y[FPP_MIN].used || f->points.y[FPP_MID].used) continue;
        if (!f->points.y[FPP_MAX].used) continue;

        /* Container top = 0 in relative coords (pmax-only frame has no pmin). */
        float container_y = scr_frame_abs_y(p);

        /* Scan ALL descendants for max y-extent relative to this container. */
        float max_extent = 0;
        for (uint32_t c = 1; c < num_frames; c++) {
            if (frames[c].size.height == 0) continue;
            /* Walk parent chain to see if this frame is a descendant of p. */
            uint32_t anc = c;
            while (anc > 0 && anc < num_frames && anc != p) anc = frames[anc].parent;
            if (anc != p) continue;
            float abs_y = scr_frame_abs_y(c);
            float extent = (abs_y - container_y) + frames[c].size.height;
            if (extent > max_extent) max_extent = extent;
        }
        if (max_extent > 0)
            ((LPUIFRAME)f)->size.height = max_extent;
    }
}

LPCUIFRAME SCR_ClearLayer(handle_t data, uint32_t layer) {
    uint32_t layout_size = 0;
    uint8_t * layout_data = (uint8_t *)data;

    layout_runtime_layer = layer < MAX_LAYOUT_LAYERS ? layer : MAX_LAYOUT_LAYERS;

    memset(runtimes, 0, sizeof(runtimes));
    memset(frames, 0, sizeof(frames));
    num_frames = 0;
    rect_t scene = SCR_LayoutSceneRect();
    frames[0].size.width = scene.w;
    frames[0].size.height = scene.h;
    frames[0].flags.type = FT_SCREEN;
    runtimes[0].rect = scene;
    runtimes[0].calculated = true;

    if (!layout_data) {
        return frames;
    }

    memcpy(&layout_size, layout_data, sizeof(layout_size));

    sizeBuf_t msg = {
        .data = layout_data + sizeof(layout_size),
        .cursize = layout_size,
        .readcount = 0,
    };
    while (true) {
        uint32_t bits = 0;
        if (msg.readcount + sizeof(uint32_t) + sizeof(uint16_t) > msg.cursize) {
            break;
        }
        uint32_t nument = MSG_ReadEntityBits(&msg, &bits);
        if (nument == 0 && bits == 0)
            break;
        if (nument >= MAX_LAYOUT_OBJECTS) {
            break;
        }
        LPUIFRAME ent = &frames[nument];
        ent->tex.coord[1] = 0xff;
        ent->tex.coord[3] = 0xff;
        MSG_ReadDeltaUIFrame(&msg, ent, nument, bits);
        if (msg.readcount + sizeof(uint8_t) > msg.cursize) {
            break;
        }
        /* Buffer length is an unsigned wire byte; values 128..255 must not sign-extend. */
        ent->buffer.size = (uint8_t)MSG_ReadByte(&msg);
        if (msg.readcount + ent->buffer.size > msg.cursize) {
            break;
        }
        ent->buffer.data = msg.data + msg.readcount;
        msg.readcount += ent->buffer.size;
        num_frames = MAX(num_frames, nument+1);
    }
    SCR_InferContainerHeights();
    return frames;
}

LPCUIFRAME SCR_Clear(handle_t data) {
    return SCR_ClearLayer(data, MAX_LAYOUT_LAYERS);
}

/* Window packets keep frame text in one trailing arena and encode frame string fields as uint32_t offsets. */
LPCUIFRAME SCR_ClearWindow(handle_t data) {
    uint32_t layout_size = 0, text_size, frame_end;
    uint8_t * layout_data = data;
    sizeBuf_t msg, scan;
    cstring_t text;

    SCR_Clear(NULL);
    if (!layout_data) return frames;
    memcpy(&layout_size, layout_data, sizeof(layout_size));
    msg = MAKE(sizeBuf_t, .data = layout_data + sizeof(layout_size), .cursize = layout_size);
    scan = msg;
    while (scan.readcount + sizeof(uint32_t) + sizeof(uint16_t) <= scan.cursize) {
        UIFRAME ent = { 0 };
        uint32_t bits, number = MSG_ReadEntityBits(&scan, &bits);
        if (!number && !bits) break;
        if (!MSG_ReadDeltaUIWindowFrame(&scan, &ent, number, bits) || scan.readcount >= scan.cursize) return frames;
        uint32_t payload = (uint8_t)MSG_ReadByte(&scan);
        if (payload > scan.cursize - scan.readcount) return frames;
        scan.readcount += payload;
    }
    frame_end = scan.readcount;
    if (scan.readcount + sizeof(uint32_t) > scan.cursize) return frames;
    text_size = MSG_ReadLong(&scan);
    if (text_size > scan.cursize - scan.readcount) return frames;
    text = (cstring_t)(scan.data + scan.readcount);
    msg.cursize = frame_end;
    while (msg.readcount + sizeof(uint32_t) + sizeof(uint16_t) <= msg.cursize) {
        uint32_t bits, number = MSG_ReadEntityBits(&msg, &bits);
        if (!number && !bits) break;
        if (number >= MAX_LAYOUT_OBJECTS) return frames;
        LPUIFRAME ent = &frames[number];
        ent->tex.coord[1] = ent->tex.coord[3] = 0xff;
        if (!MSG_ReadDeltaUIWindowFrame(&msg, ent, number, bits) || msg.readcount >= msg.cursize) return frames;
        ent->text = ent->text ? text + (uint32_t)(uintptr_t)ent->text : NULL;
        ent->tooltip = ent->tooltip ? text + (uint32_t)(uintptr_t)ent->tooltip : NULL;
        ent->onclick = ent->onclick ? text + (uint32_t)(uintptr_t)ent->onclick : NULL;
        ent->buffer.size = (uint8_t)MSG_ReadByte(&msg);
        if (ent->buffer.size > msg.cursize - msg.readcount) return frames;
        ent->buffer.data = msg.data + msg.readcount;
        msg.readcount += ent->buffer.size;
        num_frames = MAX(num_frames, number + 1);
    }
    SCR_InferContainerHeights();
    return frames;
}

void SCR_SetLayoutRoot(rect_t const * root) {
    if (!root) return;
    frames[0].size.width = root->w; frames[0].size.height = root->h;
    runtimes[0].rect = *root; runtimes[0].calculated = true;
}


uint32_t SCR_NumFrames(void) {
    return num_frames;
}

LPUIFRAME SCR_Frame(uint32_t number) {
    if (number >= MAX_LAYOUT_OBJECTS) {
        return NULL;
    }
    return frames + number;
}
