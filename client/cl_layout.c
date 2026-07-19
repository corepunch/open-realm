#include "client.h"

#define PLAYERSTATE_RESOURCE_FOOD_CAP 4
#define PLAYERSTATE_RESOURCE_FOOD_USED 5

static UIFRAME frames[MAX_LAYOUT_OBJECTS];
static DWORD num_frames = 0;

struct {
    RECT rect;
    bool calculated;
} runtimes[MAX_LAYOUT_OBJECTS];

/* Fixed 0.8x0.6 canvas regardless of window aspect: the final FDF->pixel
 * conversion already stretches this to fill the window, so HUD panels
 * anchored to different edges stay flush against each other instead of
 * drifting apart on non-4:3 resolutions. */
static RECT SCR_GetUISceneRect(void) {
    return MAKE(RECT, 0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
}

VECTOR2 get_x(LPCRECT rect) {
    return (VECTOR2) { rect->x, rect->x + rect->w };
}

VECTOR2 get_y(LPCRECT rect) {
    return (VECTOR2) { rect->y, rect->y + rect->h };
}

VECTOR2 SCR_GetAxisBounds(LPCRECT rect, bool is_x_axis) {
    return is_x_axis ? get_x(rect) : get_y(rect);
}

FLOAT SCR_NormalizeAnchorOffset(uiFramePoint_t const *p, bool is_x_axis) {
    SHORT offset = is_x_axis ? p->offset : -p->offset;
    return offset / UI_FRAMEPOINT_SCALE;
}

LPCRECT SCR_LayoutRectByNumber(LPCUIFRAME context, DWORD number) {
    if (number == UI_PARENT) {
        return SCR_LayoutRect(frames+context->parent);
    } else {
        return SCR_LayoutRect(frames+number);
    }
}

FLOAT SCR_GetAnchor(LPCUIFRAME f,
                    uiFramePoint_t const *p,
                    VECTOR2 (*get)(LPCRECT))
{
    bool const is_x_axis = (get == get_x);
    VECTOR2 b = SCR_GetAxisBounds(SCR_LayoutRectByNumber(f, p->relativeTo), is_x_axis);
    FLOAT offset = SCR_NormalizeAnchorOffset(p, is_x_axis);
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
                              FLOAT width,
                              bool is_x_axis)
{
    uiFramePoint_t const *pmin = points + FPP_MIN;
    uiFramePoint_t const *pmid = points + FPP_MID;
    uiFramePoint_t const *pmax = points + FPP_MAX;
    VECTOR2 (*get)(LPCRECT) = is_x_axis ? get_x : get_y;

    if (pmid->used) {
        return (VECTOR2) {
            SCR_GetAnchor(frame, pmid, get) - width / 2,
            width,
        };
    } else if (pmin->used && pmax->used) {
        FLOAT anchor_min = SCR_GetAnchor(frame, pmin, get);
        FLOAT anchor_max = SCR_GetAnchor(frame, pmax, get);
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
                     FLOAT width,
                     VECTOR2 (*get)(LPCRECT))
{
    return SCR_SolveAxisPosition(frame, p, width, get == get_x);
}

LPCSTR SCR_GetStringValue(LPCUIFRAME frame) {
    static char text[1024] = { 0 };
    if (frame->stat >= MAX_STATS) {
        if (cl.playerstate.texts[frame->stat - MAX_STATS]) {
            strlcpy(text, cl.playerstate.texts[frame->stat - MAX_STATS], sizeof(text));
        } else {
            memset(text, 0, sizeof(text));
        }
    } else if (frame->stat == PLAYERSTATE_RESOURCE_FOOD_USED) {
        DWORD food_used = cl.playerstate.stats[PLAYERSTATE_RESOURCE_FOOD_USED];
        DWORD food_made = cl.playerstate.stats[PLAYERSTATE_RESOURCE_FOOD_CAP];
        snprintf(text, sizeof(text), "%d/%d", food_used, food_made);
    } else if (frame->stat > 0) {
        snprintf(text, sizeof(text), "%d", cl.playerstate.stats[frame->stat]);
    } else if (frame->text) {
        return frame->text;
    } else {
        text[0] = '\0';
    }
    return text;
}

drawText_t SCR_GetDrawText(LPCUIFRAME frame,
                      FLOAT avl_width,
                      LPCSTR text,
                      uiLabel_t const *label)
{
    LPCFONT font = cl.fonts[label->font];
    return MAKE(drawText_t,
                .font = font,
                .text = text,
                .color = frame->color,
                .halign = label->textalignx,
                .valign = label->textaligny,
                .icons = cl.pics,
                .lineHeight = 1.33,
                .textWidth = avl_width);
}

LPCRECT SCR_LayoutRect(LPCUIFRAME frame) {
    if (runtimes[frame->number].calculated) {
        return &runtimes[frame->number].rect;
    } else {
        runtimes[frame->number].calculated = true; // done here to avoid recursion
    }
    VECTOR2 elemsize = {0};
    FLOAT avl_space = runtimes[0].rect.w;
    drawText_t drawtext = {0};
    switch (frame->flags.type) {
        case FT_STRING:
        case FT_TEXT:
            if (frame->size.width > 0) {
                avl_space = frame->size.width;
            }
            drawtext = SCR_GetDrawText(frame, avl_space, SCR_GetStringValue(frame), frame->buffer.data);
            elemsize = re.GetTextSize(&drawtext);
            break;
        case FT_TEXTURE:
        case FT_SIMPLESTATUSBAR: {
            /* NormalImage/HoverImage semantics: when the frame has no explicit
               size AND no anchors on either axis, it fills the parent rect
               completely (SC2 button image fill-parent behaviour). */
            BOOL has_x_anchor = frame->points.x[FPP_MIN].used || frame->points.x[FPP_MID].used || frame->points.x[FPP_MAX].used;
            BOOL has_y_anchor = frame->points.y[FPP_MIN].used || frame->points.y[FPP_MID].used || frame->points.y[FPP_MAX].used;
            BOOL no_explicit_size = frame->size.width == 0 && frame->size.height == 0;
            if (no_explicit_size && !has_x_anchor && !has_y_anchor) {
                /* Fill parent: copy parent rect directly. */
                LPCRECT pr = SCR_LayoutRect(frames + frame->parent);
                runtimes[frame->number].rect = *pr;
                return &runtimes[frame->number].rect;
            }
            if (frame->size.width > 0 && frame->size.height > 0) {
                elemsize.x = frame->size.width;
                elemsize.y = frame->size.height;
            } else {
                LPCRECT pr = SCR_LayoutRect(frames + frame->parent);
                elemsize.x = pr->w;
                elemsize.y = pr->h;
            }
            break;
        }
        default:
            break;
    }
    if (frame->size.width == 0 && !(frame->points.x[FPP_MIN].used && frame->points.x[FPP_MAX].used)) {
        ((LPUIFRAME )frame)->size.width = elemsize.x;
    }
    if (frame->size.height == 0 && !(frame->points.y[FPP_MIN].used && frame->points.y[FPP_MAX].used)) {
        ((LPUIFRAME )frame)->size.height = elemsize.y;
    }
    VECTOR2 const rect[] = {
        get_position(frame, frame->points.x, frame->size.width, get_x),
        get_position(frame, frame->points.y, frame->size.height, get_y),
    };
    runtimes[frame->number].rect = (RECT) {
        .x = rect[0].x,
        .y = rect[1].x,
        .w = rect[0].y,
        .h = rect[1].y,
    };
    return &runtimes[frame->number].rect;
}

/* Resolve the y-coordinate of frame 'idx' relative to a pmax-only container's
 * implicit top (which we treat as 0).  Follows pmin_y anchor chains, resolving
 * FPP_MAX target to (ref_y + ref_h).  Used only by SCR_InferContainerHeights
 * before runtimes[] are populated — never reads runtimes[]. */
static FLOAT scr_frame_abs_y(DWORD idx) {
    if (idx == 0 || idx >= num_frames) return 0;
    LPCUIFRAME f = &frames[idx];
    uiFramePoint_t const *pmin_y = &f->points.y[FPP_MIN];
    if (!pmin_y->used) return 0;
    DWORD rel = pmin_y->relativeTo;
    FLOAT parent_y = (rel == UI_PARENT) ? scr_frame_abs_y(f->parent) :
                     (rel < num_frames) ? scr_frame_abs_y(rel) : 0;
    FLOAT off = -((FLOAT)pmin_y->offset / UI_FRAMEPOINT_SCALE);
    if (pmin_y->targetPos == FPP_MAX) {
        FLOAT parent_h = (rel == UI_PARENT) ? frames[f->parent].size.height :
                         (rel < num_frames) ? frames[rel].size.height : 0;
        return parent_y + parent_h + off;
    }
    return parent_y + off;
}

/* SC2 panels (CommandPanel, InfoPanel, etc.) often have only a Bottom anchor and
 * no explicit Height.  After wire parsing, for any FT_FRAME with size.height==0
 * and only pmax_y set, infer height from the max y-extent of all descendants. */
static void SCR_InferContainerHeights(void) {
    for (DWORD p = num_frames; p-- > 1; ) {
        LPCUIFRAME f = &frames[p];
        if (f->size.height > 0) continue;
        if (f->flags.type != FT_FRAME) continue;
        if (f->points.y[FPP_MIN].used || f->points.y[FPP_MID].used) continue;
        if (!f->points.y[FPP_MAX].used) continue;

        /* Container top = 0 in relative coords (pmax-only frame has no pmin). */
        FLOAT container_y = scr_frame_abs_y(p);

        /* Scan ALL descendants for max y-extent relative to this container. */
        FLOAT max_extent = 0;
        for (DWORD c = 1; c < num_frames; c++) {
            if (frames[c].size.height == 0) continue;
            /* Walk parent chain to see if this frame is a descendant of p. */
            DWORD anc = c;
            while (anc > 0 && anc < num_frames && anc != p) anc = frames[anc].parent;
            if (anc != p) continue;
            FLOAT abs_y = scr_frame_abs_y(c);
            FLOAT extent = (abs_y - container_y) + frames[c].size.height;
            if (extent > max_extent) max_extent = extent;
        }
        if (max_extent > 0)
            ((LPUIFRAME)f)->size.height = max_extent;
    }
}

LPCUIFRAME SCR_Clear(HANDLE data) {
    DWORD layout_size = 0;
    LPBYTE layout_data = (LPBYTE)data;

    memset(runtimes, 0, sizeof(runtimes));
    memset(frames, 0, sizeof(frames));
    num_frames = 0;
    RECT scene = SCR_GetUISceneRect();
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
        DWORD bits = 0;
        if (msg.readcount + sizeof(DWORD) + sizeof(WORD) > msg.cursize) {
            break;
        }
        DWORD nument = MSG_ReadEntityBits(&msg, &bits);
        if (nument == 0 && bits == 0)
            break;
        if (nument >= MAX_LAYOUT_OBJECTS) {
            break;
        }
        LPUIFRAME ent = &frames[nument];
        ent->tex.coord[1] = 0xff;
        ent->tex.coord[3] = 0xff;
        MSG_ReadDeltaUIFrame(&msg, ent, nument, bits);
        if (msg.readcount + sizeof(BYTE) > msg.cursize) {
            break;
        }
        ent->buffer.size = MSG_ReadByte(&msg);
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


DWORD SCR_NumFrames(void) {
    return num_frames;
}

LPUIFRAME SCR_Frame(DWORD number) {
    if (number >= MAX_LAYOUT_OBJECTS) {
        return NULL;
    }
    return frames + number;
}
