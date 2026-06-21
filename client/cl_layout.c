#include "client.h"

#define PLAYERSTATE_RESOURCE_FOOD_CAP 4
#define PLAYERSTATE_RESOURCE_FOOD_USED 5

static UIFRAME frames[MAX_LAYOUT_OBJECTS];
static DWORD num_frames = 0;

struct {
    RECT rect;
    bool calculated;
} runtimes[MAX_LAYOUT_OBJECTS];

static RECT SCR_GetUISceneRect(void) {
    size2_t window = re.GetWindowSize();
    FLOAT window_aspect = UI_MIN_ASPECT;
    FLOAT x_scale = 1.0f;
    FLOAT y_scale = 1.0f;

    if (window.width > 0 && window.height > 0) {
        window_aspect = (FLOAT)window.width / (FLOAT)window.height;
    }

    if (window_aspect > UI_MIN_ASPECT) {
        x_scale = window_aspect / UI_MIN_ASPECT;
    } else if (window_aspect < UI_MIN_ASPECT) {
        y_scale = UI_MIN_ASPECT / window_aspect;
    }

    FLOAT scene_w = UI_BASE_WIDTH * x_scale;
    FLOAT scene_h = UI_BASE_HEIGHT * y_scale;
    return MAKE(RECT,
                (UI_BASE_WIDTH - scene_w) * 0.5f,
                (UI_BASE_HEIGHT - scene_h) * 0.5f,
                scene_w,
                scene_h);
}

VECTOR2 SCR_MouseToFdf(void) {
    size2_t window = re.GetWindowSize();
    RECT scene = SCR_GetUISceneRect();
    FLOAT nx = 0;
    FLOAT ny = 0;

    if (window.width > 0) {
        nx = (FLOAT)mouse.origin.x / (FLOAT)window.width;
    }
    if (window.height > 0) {
        ny = (FLOAT)mouse.origin.y / (FLOAT)window.height;
    }

    return MAKE(VECTOR2,
                scene.x + nx * scene.w,
                scene.y + ny * scene.h);
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
            strcpy(text, cl.playerstate.texts[frame->stat - MAX_STATS]);
        } else {
            memset(text, 0, sizeof(text));
        }
    } else if (frame->stat == PLAYERSTATE_RESOURCE_FOOD_USED) {
        DWORD food_used = cl.playerstate.stats[PLAYERSTATE_RESOURCE_FOOD_USED];
        DWORD food_made = cl.playerstate.stats[PLAYERSTATE_RESOURCE_FOOD_CAP];
        sprintf(text, "%d/%d", food_used, food_made);
    } else if (frame->stat > 0) {
        sprintf(text, "%d", cl.playerstate.stats[frame->stat]);
    } else if (frame->text) {
        return frame->text;
    } else {
        sprintf(text, "text %d", frame->number);
    }
    return text;
}

drawText_t SCR_GetDrawText(LPCUIFRAME frame,
                      FLOAT avl_width,
                      LPCSTR text,
                      uiLabel_t const *label)
{
    return MAKE(drawText_t,
                .font = cl.fonts[label->font],
                .text = text,
                .color = frame->color,
                .halign = label->textalignx,
                .valign = label->textaligny,
                .icons = cl.pics,
                .lineHeight = 1.33,
                .wordWrap = true,
                .textWidth = avl_width);
}

LPCRECT SCR_LayoutRect(LPCUIFRAME frame) {
    if (runtimes[frame->number].calculated) {
        return &runtimes[frame->number].rect;
    } else {
        runtimes[frame->number].calculated = true; // done here to avoid recursion
    }
    VECTOR2 elemsize;
    size2_t imagesize;
    FLOAT avl_space = runtimes[0].rect.w;
    drawText_t drawtext;
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
        case FT_SIMPLESTATUSBAR:
            imagesize = re.GetTextureSize(cl.pics[frame->tex.index]);
            elemsize.x = imagesize.width * 8;
            elemsize.y = imagesize.height * 6;
            break;
        default:
            break;
    }
    if (frame->size.width == 0) {
        ((LPUIFRAME )frame)->size.width = elemsize.x;
    }
    if (frame->size.height == 0) {
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
        if (msg.readcount + sizeof(WORD) > msg.cursize) {
            break;
        }
        ent->buffer.size = MSG_ReadShort(&msg);
        if (msg.readcount + ent->buffer.size > msg.cursize) {
            break;
        }
        ent->buffer.data = msg.data + msg.readcount;
        msg.readcount += ent->buffer.size;
        num_frames = MAX(num_frames, nument+1);
    }
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
