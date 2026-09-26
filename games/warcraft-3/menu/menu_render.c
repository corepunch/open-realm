/*
 * menu_render.c — Frame rendering and layout solving
 *
 * This module implements:
 * 1. Layout solving: Calculate screen positions from SetPoint anchors
 * 2. Frame rendering: Draw backdrops, textures, text, portraits
 * 3. Frame hierarchy traversal: Recursively render child frames
 *
 * Ported from client/cl_scrn.c (Phase 7 consolidation)
 */

#include "menu_local.h"
#include "client/menu_text_input.h"
#if defined(__has_include)
#if __has_include(<SDL2/SDL_keycode.h>)
#include <SDL2/SDL_keycode.h>
#endif
#endif

#ifndef SDLK_BACKSPACE
#define SDLK_BACKSPACE 8
#define SDLK_DELETE 127
#define SDLK_LEFT 1073741904
#define SDLK_RIGHT 1073741903
#define SDLK_HOME 1073741898
#define SDLK_END 1073741901
#define SDLK_RETURN 13
#define SDLK_KP_ENTER 1073741912
#define SDLK_ESCAPE 27
#endif

#define MAX_FRAME_DEPTH 64
#define NUM_BACKDROP_CORNERS 8

#ifndef true
#define true 1
#define false 0
#endif

/* Runtime layout state (cached screen rects) */
typedef struct {
    rect_t rect, layout;
    bool calculated;
} frameRuntime_t;

static frameRuntime_t runtimes[MAX_UI_CLASSES];
static rect_t scene_rect;
static bool scene_rect_valid = false;
static bool animate_frames;
static frameDef_t const *active_slider = NULL;
static frameDef_t const *active_popup = NULL;
static frameDef_t const *active_modal = NULL;
static frameDef_t *active_edit = NULL;
static menuTextInput_t active_ti;
static int active_popup_hover_item = -1;

static bool UI_FrameIndex(frameDef_t const *frame, uint32_t *index) {
    if (!frame || frame < frames || frame >= frames + MAX_UI_CLASSES) {
        return false;
    }
    *index = (uint32_t)(frame - frames);
    return true;
}

static bool UI_TextHasLineBreak(cstring_t text) {
    for (cstring_t p = text ? text : ""; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            return true;
        }
        if (*p == '|' && (p[1] == 'n' || p[1] == 'N')) {
            return true;
        }
    }
    return false;
}

static cstring_t UI_FontFile(cstring_t name) {
    return Theme_String(name && *name ? name : "MasterFont", "Default");
}

static uint32_t UI_FontPixelSize(float size) {
    return size > 0 ? (uint32_t)(size * 1000.0f + 0.5f) : 13;
}

/* Forward declarations */
static rect_t const *UI_LayoutRect(frameDef_t const *frame);
static rect_t const *UI_LayoutBase(frameDef_t const *frame);
static void UI_DrawFrameOne(frameDef_t const *frame);
static bool UI_FrameWithinRoot(frameDef_t const *root, frameDef_t const *frame);
static bool UI_PointerBlockedByModal(frameDef_t const *frame);
static bool UI_PointerBlockedByPopup(frameDef_t const *frame);
static frameDef_t const *UI_FindActiveModalRoot(frameDef_t const *const *roots, uint32_t num_roots);

/* ========================================================================
 * HIT TESTING — used by event handlers to find frames under the cursor
 * ======================================================================== */

static bool UI_PointInRect(float x, float y, rect_t const *rect) {
    return rect && x >= rect->x && x < rect->x + rect->w &&
           y >= rect->y && y < rect->y + rect->h;
}

static bool UI_FrameIsInteractive(frameDef_t const *frame) {
    if (!frame || frame->hidden || frame->disabled || (frame->ui_flags & UIFLAG_PASSTHROUGH)) {
        return false;
    }
    switch (frame->Type) {
        case FT_BUTTON: case FT_GLUEBUTTON: case FT_SIMPLEBUTTON:
        case FT_TEXTBUTTON: case FT_GLUETEXTBUTTON: case FT_COMMANDBUTTON:
        case FT_CHECKBOX: case FT_GLUECHECKBOX: case FT_SIMPLECHECKBOX:
        case FT_SLIDER:
        case FT_EDITBOX: case FT_GLUEEDITBOX: case FT_SLASHCHATBOX:
        case FT_MENU:
        case FT_POPUPMENU: case FT_GLUEPOPUPMENU:
        case FT_LISTBOX:
            return true;
        case FT_CONTROL:
            return frame->MapListControl.State != NULL;
        case FT_FRAME: case FT_SIMPLEFRAME:
            return frame->OnClick[0] || frame->MapListControl.State != NULL;
        default:
            return false;
    }
}

/* Walk the layout cache back-to-front and return the topmost interactive frame
 * at the given FDF-space coordinates. Returns NULL if nothing was hit. */
frameDef_t const *UI_HitTest(float fdf_x, float fdf_y) {
    for (int i = MAX_UI_CLASSES - 1; i >= 0; i--) {
        if (!runtimes[i].calculated) {
            continue;
        }
        frameDef_t const *frame = &frames[i];
        if (!frame->inuse || !UI_FrameIsInteractive(frame)) {
            continue;
        }
        if (UI_PointerBlockedByPopup(frame)) {
            continue;
        }
        if (UI_PointInRect(fdf_x, fdf_y, &runtimes[i].rect)) {
            return frame;
        }
    }
    return NULL;
}

/* ========================================================================
 * LAYOUT SOLVING
 * ======================================================================== */

/* Glue frames anchor to the scene the engine canvas resolved and the renderer projects, so widescreen policy
 * (stretched classic canvas or widened 1.30+ canvas) lives in one place; a test without a renderer keeps the
 * authored scene. */
rect_t UI_GetSceneRect(void) {
    if (scene_rect_valid) {
        return scene_rect;
    }
    refExport_t *renderer = mi.GetRenderer();
    scene_rect = renderer ? renderer->GetUISceneRect() : (rect_t) { 0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT };
    scene_rect_valid = true;
    return scene_rect;
}

rect_t UI_GetCenteredSceneRect(void) {
    rect_t scene = UI_GetSceneRect();
    if (scene.w > UI_BASE_WIDTH)
        scene.x = (scene.w - UI_BASE_WIDTH) * 0.5f, scene.w = UI_BASE_WIDTH;
    return scene;
}

static vector2_t UI_GetXBounds(rect_t const *rect) {
    return (vector2_t) { rect->x, rect->x + rect->w };
}

static vector2_t UI_GetYBounds(rect_t const *rect) {
    return (vector2_t) { rect->y, rect->y + rect->h };
}

static vector2_t UI_GetAxisBounds(rect_t const *rect, bool is_x_axis) {
    return is_x_axis ? UI_GetXBounds(rect) : UI_GetYBounds(rect);
}

static float UI_NormalizeAnchorOffset(framePoint_t const *p, bool is_x_axis) {
    return is_x_axis ? p->offset : -p->offset;
}

static rect_t const *UI_GetRelativeRect(frameDef_t const *frame, frameDef_t const *relativeTo) {
    if (!relativeTo) {
        /* Anchor to root scene */
        return &scene_rect;
    }
    if (relativeTo == frame->Parent) {
        return UI_LayoutBase(frame->Parent);
    }
    return UI_LayoutBase(relativeTo);
}

static float UI_GetAnchor(frameDef_t const *frame,
                         framePoint_t const *p,
                         bool is_x_axis)
{
    vector2_t b = UI_GetAxisBounds(UI_GetRelativeRect(frame, p->relativeTo), is_x_axis);
    float offset = UI_NormalizeAnchorOffset(p, is_x_axis);
    
    if (p->targetPos == FPP_MID) {
        return (b.x + b.y) / 2.0f + offset;
    } else if (p->targetPos == FPP_MAX) {
        return b.y + offset;
    } else {
        return b.x + offset;
    }
}

static vector2_t UI_SolveAxisPosition(frameDef_t const *frame,
                                   framePoint_t const *points,
                                   float size,
                                   bool is_x_axis)
{
    framePoint_t const *pmin = &points[FPP_MIN];
    framePoint_t const *pmid = &points[FPP_MID];
    framePoint_t const *pmax = &points[FPP_MAX];

    if (pmid->used) {
        /* Center anchor: position = mid - size/2 */
        return (vector2_t) {
            UI_GetAnchor(frame, pmid, is_x_axis) - size / 2.0f,
            size,
        };
    } else if (pmin->used && pmax->used) {
        /* Both min and max: stretch between anchors */
        float anchor_min = UI_GetAnchor(frame, pmin, is_x_axis);
        float anchor_max = UI_GetAnchor(frame, pmax, is_x_axis);
        return (vector2_t) {
            anchor_min,
            anchor_max - anchor_min,
        };
    } else if (pmax->used) {
        /* Max anchor only: position = max - size */
        return (vector2_t) {
            UI_GetAnchor(frame, pmax, is_x_axis) - size,
            size,
        };
    } else if (pmin->used) {
        /* Min anchor only: position = min */
        return (vector2_t) {
            UI_GetAnchor(frame, pmin, is_x_axis),
            size,
        };
    }
    
    /* No anchors set: default to (0,0) with given size */
    return (vector2_t) { 0, size };
}

static rect_t const *UI_LayoutBase(frameDef_t const *frame) {
    static rect_t uncached_rect;
    uint32_t frame_index;
    rect_t *out;

    if (!frame) {
        return &scene_rect;
    }
    
    /* Check cache */
    if (UI_FrameIndex(frame, &frame_index) && runtimes[frame_index].calculated) {
        return &runtimes[frame_index].layout;
    }
    out = UI_FrameIndex(frame, &frame_index) ? &runtimes[frame_index].layout : &uncached_rect;
    
    /* Mark as calculated to prevent recursion */
    if (UI_FrameIndex(frame, &frame_index)) {
        runtimes[frame_index].calculated = true;
    }
    
    /* Calculate intrinsic size based on frame type */
    float intrinsic_w = frame->Width;
    float intrinsic_h = frame->Height;
    
    if (intrinsic_w == 0 || intrinsic_h == 0) {
        /* Try to derive size from content */
        switch (frame->Type) {
            case FT_TEXT:
            case FT_STRING:
                if (frame->Text && frame->Font.Index) {
                    bool auto_width = intrinsic_w == 0;
                    bool auto_height = intrinsic_h == 0;
                    refExport_t *renderer = mi.GetRenderer();
                    drawText_t dt = {
                        .font = renderer ? renderer->LoadFont(UI_FontFile(frame->Font.Name),
                                                              UI_FontPixelSize(frame->Font.Size)) : NULL,
                        .text = frame->Text,
                        .textWidth = intrinsic_w > 0 ? intrinsic_w : 0.0f,
                        .lineHeight = 1.0f,
                        .flags = (intrinsic_w > 0) ? DRAW_WORD_WRAP : 0,
                    };
                    vector2_t text_size = renderer ? renderer->GetTextSize((drawText_t const *)&dt) : MAKE(vector2_t, 0, 0);
                    if (auto_width) {
                        intrinsic_w = text_size.x;
                    }
                    if (auto_height) {
                        /*
                         * FDF single-line labels are spaced from the declared
                         * font size, not from the TrueType line box. FRIZQT's
                         * metrics are taller than the requested size, and using
                         * that height here makes chained labels drift away from
                         * sibling controls.
                         */
                        intrinsic_h = (auto_width &&
                                       frame->Font.Size > 0.0f &&
                                       !UI_TextHasLineBreak(frame->Text))
                                      ? frame->Font.Size
                                      : text_size.y;
                    }
                }
                break;
            case FT_TEXTURE:
            case FT_BACKDROP:
                if (frame->Texture.Image) {
                    refExport_t *renderer = mi.GetRenderer();
                    texture_t const *texture = UI_GetTexture(frame->Texture.Image);
                    size2_t tex_size = (renderer && texture) ? renderer->GetTextureSize(texture) : MAKE(size2_t, 0, 0);
                    if (intrinsic_w == 0) intrinsic_w = tex_size.width / 1000.0f;  /* Normalize to 0-1 space */
                    if (intrinsic_h == 0) intrinsic_h = tex_size.height / 1000.0f;
                }
                break;
            case FT_SPRITE:
                /*
                 * WC3 SPRITE frames position the MDX instance at the assigned
                 * anchor point. Many glue sprites have screen-space vertices
                 * baked into the model and intentionally omit Width/Height.
                 */
                break;
            case FT_FRAME:
            case FT_SIMPLEFRAME:
                /*
                 * Structural FDF containers do not acquire an implicit size.
                 * Un-sized containers such as LoadingCustomPanel are 0x0 in
                 * Warcraft/Warsmash unless anchors stretch an axis.
                 */
                break;
            default:
                /* Default size if not specified */
                if (intrinsic_w == 0) intrinsic_w = 0.1f;
                if (intrinsic_h == 0) intrinsic_h = 0.1f;
                break;
        }
    }
    
    /* Solve X and Y positions */
    vector2_t x_pos = UI_SolveAxisPosition(frame, frame->Points.x, intrinsic_w, true);
    vector2_t y_pos = UI_SolveAxisPosition(frame, frame->Points.y, intrinsic_h, false);
    
    *out = (rect_t) {
        .x = x_pos.x,
        .y = y_pos.x,
        .w = x_pos.y,
        .h = y_pos.y,
    };
    
    return out;
}

/* Resolve anchors in native FDF space, then translate each result once so motion
 * cannot accumulate through child/sibling chains or cross-side references. */
static rect_t const *UI_LayoutRect(frameDef_t const *frame) {
    rect_t const *base = UI_LayoutBase(frame);
    uint32_t idx;
    if (!UI_FrameIndex(frame, &idx)) return base;
    runtimes[idx].rect = *base;
    if (animate_frames) runtimes[idx].rect.y += UI_ScreenFrameOffset(frame);
    return &runtimes[idx].rect;
}

/* ========================================================================
 * FRAME RENDERING
 * ======================================================================== */

static void UI_DrawTexture(frameDef_t const *frame, rect_t const *rect) {
    refExport_t *renderer = mi.GetRenderer();

    if (!frame->Texture.Image) {
        return;
    }

    if (!renderer || !renderer->DrawImageEx) {
        return;
    }

    texture_t const *tex = UI_GetTexture(frame->Texture.Image);
    if (!tex) {
        return;
    }
    
    /* Use TexCoord if specified, otherwise full texture */
    /* BOX2 has min/max, not x/y/w/h */
    rect_t uv;
    if (frame->Texture.TexCoord.min.x != 0 || frame->Texture.TexCoord.min.y != 0 ||
        frame->Texture.TexCoord.max.x != 0 || frame->Texture.TexCoord.max.y != 0) {
        uv = (rect_t) {
            frame->Texture.TexCoord.min.x,
            frame->Texture.TexCoord.min.y,
            frame->Texture.TexCoord.max.x - frame->Texture.TexCoord.min.x,
            frame->Texture.TexCoord.max.y - frame->Texture.TexCoord.min.y
        };
    } else {
        uv = (rect_t) { 0, 0, 1, 1 };
    }
    
    drawImage_t di = {
        .texture = tex,
        .shader = SHADER_UI,
        .alphamode = frame->AlphaMode,
        .screen = *rect,
        .uv = uv,
        .color = frame->Color,
    };
    
    renderer->DrawImageEx((drawImage_t const *)&di);
}

static void UI_DrawText(frameDef_t const *frame, rect_t const *rect) {
    refExport_t *renderer = mi.GetRenderer();
    cstring_t font_name;
    uint32_t font_size;
    rect_t text_rect = *rect;

    if (!frame->Text || !*frame->Text) {
        return;
    }

    if (!renderer || !renderer->LoadFont || !renderer->DrawText) {
        return;
    }

    font_name = UI_FontFile(frame->Font.Name);
    font_size = UI_FontPixelSize(frame->Font.Size);
    font_t const *font = renderer->LoadFont(font_name, font_size);
    if (!font) {
        return;
    }
    
    text_rect.x += frame->Font.Justification.Offset.x;
    text_rect.y += frame->Font.Justification.Offset.y;

    color32_t color = frame->Font.Color;
    if (color.a == 0 && (color.r || color.g || color.b)) {
        color.a = 255;
    }

    drawText_t dt = {
        .font = font,
        .text = frame->Text,
        .rect = text_rect,
        .color = color,
        .textWidth = text_rect.w,
        .lineHeight = 1.0f,
        .flags = (frame->Width > 0) ? DRAW_WORD_WRAP : 0,
        .halign = frame->Font.Justification.Horizontal,
        .valign = frame->Font.Justification.Vertical,
    };
    
    renderer->DrawText((drawText_t const *)&dt);
}

static void UI_DrawHighlightFrame(frameDef_t const *frame, rect_t const *rect);

#include "controls/menu_control_backdrop.h"
#include "controls/menu_control_popup_menu.h"
#include "controls/menu_control_button.h"
#include "controls/menu_control_checkbox.h"
#include "controls/menu_control_editbox.h"
#include "controls/menu_control_map_list.h"
#include "controls/menu_control_slider.h"

/* ========================================================================
 * PER-TYPE EVENT HANDLERS — called from UI_MouseEventLocal
 * ======================================================================== */

static void UI_ButtonEventHandler(frameDef_t *frame, menuMouseEvent_t event, float fdf_x, float fdf_y, int32_t param) {
    (void)fdf_x; (void)fdf_y;
    if (param != 1) {
        return;
    }
    if (event == MENU_MOUSE_DOWN) {
        frame->ui_flags |= UIFLAG_PRESSED;
    } else if (event == MENU_MOUSE_UP) {
        frame->ui_flags &= ~UIFLAG_PRESSED;
        if (frame->OnClick[0]) {
            UI_QueueCommand(frame->OnClick);
        }
    }
}

static void UI_CheckBoxEventHandler(frameDef_t *frame, menuMouseEvent_t event, float fdf_x, float fdf_y, int32_t param) {
    (void)fdf_x; (void)fdf_y;
    if (param != 1) {
        return;
    }
    if (event == MENU_MOUSE_DOWN) {
        frame->ui_flags |= UIFLAG_PRESSED;
    } else if (event == MENU_MOUSE_UP) {
        frame->ui_flags &= ~UIFLAG_PRESSED;
        frame->ui_flags ^= UIFLAG_CHECKED;
        ((frameDef_t *)frame)->CheckBox.Checked = (frame->ui_flags & UIFLAG_CHECKED) != 0;
        if (frame->OnClick[0]) {
            UI_QueueCommand(frame->OnClick);
        }
    }
}

static void UI_SliderEventHandler(frameDef_t *frame, menuMouseEvent_t event, float fdf_x, float fdf_y, int32_t param) {
    if (event == MENU_MOUSE_DOWN && param == 1) {
        UI_SliderBeginDrag(frame, fdf_x, fdf_y);
        frame->ui_flags |= UIFLAG_PRESSED;
    } else if (event == MENU_MOUSE_UP && param == 1) {
        UI_SliderEndDrag(frame);
        frame->ui_flags &= ~UIFLAG_PRESSED;
    } else if (event == MENU_MOUSE_MOVE) {
        UI_SliderUpdateDrag(frame, fdf_x, fdf_y);
    }
}

static void UI_EditBoxEventHandler(frameDef_t *frame, menuMouseEvent_t event, float fdf_x, float fdf_y, int32_t param) {
    (void)fdf_x; (void)fdf_y;
    if (event == MENU_MOUSE_DOWN && param == 1) {
        UI_EditboxFocusOnHit(frame);
    }
}

static void UI_MapListEventHandler(frameDef_t *frame, menuMouseEvent_t event, float fdf_x, float fdf_y, int32_t param) {
    if (event == MENU_MOUSE_UP && param == 1) {
        UI_MapListSelectRow(frame, fdf_x, fdf_y);
    }
    if (event == MENU_MOUSE_SCROLL && MENU_MOUSE_PARAM_Y(param) > 0) {
        UI_MapListScroll(frame, true);
    }
    if (event == MENU_MOUSE_SCROLL && MENU_MOUSE_PARAM_Y(param) < 0) {
        UI_MapListScroll(frame, false);
    }
}

static void UI_PopupEventHandler(frameDef_t *frame, menuMouseEvent_t event, float fdf_x, float fdf_y, int32_t param) {
    (void)fdf_x; (void)fdf_y;
    if (event == MENU_MOUSE_UP && param == 1) {
        UI_TogglePopup(frame);
    }
}

static void UI_PopupMenuEventHandler(frameDef_t *frame, menuMouseEvent_t event, float fdf_x, float fdf_y, int32_t param) {
    (void)frame;
    if (event == MENU_MOUSE_SCROLL && MENU_MOUSE_PARAM_Y(param) > 0) {
        UI_PopupMenuScroll(true);
    }
    if (event == MENU_MOUSE_SCROLL && MENU_MOUSE_PARAM_Y(param) < 0) {
        UI_PopupMenuScroll(false);
    }
    if (event == MENU_MOUSE_UP && param == 1) {
        UI_PopupSelectItem(fdf_x, fdf_y);
    }
}

/* ========================================================================
 * PER-TYPE DRAW FUNCTIONS — called from UI_DrawFrameOne
 * ======================================================================== */

static void UI_ButtonDraw(frameDef_t const *frame, rect_t const *rect) {
    frameDef_t const *backdrop = UI_ButtonBackdrop(frame, rect);
    if (backdrop && backdrop->Type == FT_TEXTURE) {
        UI_DrawTexture(backdrop, rect);
    } else {
        UI_DrawBackdropWithColor(backdrop, rect, frame->Color);
    }
    UI_DrawTexture(frame, rect);
    UI_DrawButtonText(frame, rect);
}

static void UI_CheckBoxDraw(frameDef_t const *frame, rect_t const *rect) {
    frameDef_t const *backdrop = UI_CheckBoxBackdrop(frame, rect);
    UI_DrawBackdropWithColor(backdrop, rect, frame->Color);
    UI_DrawTexture(frame, rect);
    UI_DrawHighlightFrame(UI_CheckBoxCheckHighlight(frame), rect);
}

/* Wire per-type event handler and draw function pointers */
__attribute__((visibility("hidden"))) void UI_WireFrameTypeFunctions(frameDef_t *frame) {
    if (!frame) {
        return;
    }
    switch (frame->Type) {
        case FT_BUTTON: case FT_TEXTBUTTON: case FT_GLUETEXTBUTTON:
        case FT_GLUEBUTTON: case FT_SIMPLEBUTTON: case FT_COMMANDBUTTON:
            frame->event_handler = UI_ButtonEventHandler;
            frame->draw = UI_ButtonDraw;
            break;
        case FT_CHECKBOX: case FT_GLUECHECKBOX: case FT_SIMPLECHECKBOX:
            frame->event_handler = UI_CheckBoxEventHandler;
            frame->draw = UI_CheckBoxDraw;
            if (frame->CheckBox.Checked) {
                frame->ui_flags |= UIFLAG_CHECKED;
            }
            break;
        case FT_SLIDER:
            frame->event_handler = UI_SliderEventHandler;
            frame->draw = UI_DrawSlider;
            break;
        case FT_EDITBOX: case FT_GLUEEDITBOX: case FT_SLASHCHATBOX:
            frame->event_handler = UI_EditBoxEventHandler;
            frame->draw = UI_DrawEditBox;
            break;
        case FT_MENU:
            frame->event_handler = UI_PopupMenuEventHandler;
            frame->draw = UI_DrawMenu;
            break;
        case FT_POPUPMENU: case FT_GLUEPOPUPMENU:
            frame->event_handler = UI_PopupEventHandler;
            break;
        case FT_CONTROL:
            /* Warcraft MapListBox templates are CONTROL roots. */
            if (frame->MapListControl.State) {
                frame->event_handler = UI_MapListEventHandler;
            }
            break;
        case FT_FRAME: case FT_SIMPLEFRAME:
            /* Programmatic map-list roots may also be plain FRAMEs. */
            if (frame->MapListControl.State) {
                frame->event_handler = UI_MapListEventHandler;
                frame->draw = UI_DrawMapListControl;
            }
            break;
        default:
            break;
    }
}

bool UI_EditHasFocus(frameDef_t const *frame) {
    return active_edit && active_edit == frame;
}

cstring_t UI_EditValue(frameDef_t const *frame) {
    return UI_EditText(frame);
}

void UI_SetEditValue(frameDef_t *frame, cstring_t text) {
    UI_SetEditText(frame, text);
}

void UI_ClearEditFocus(void) {
    UI_FocusEdit(NULL);
}

static bool UI_FrameWithinRoot(frameDef_t const *root, frameDef_t const *frame) {
    frameDef_t const *cursor = frame;

    while (cursor) {
        if (cursor == root) {
            return true;
        }
        cursor = cursor->Parent;
    }

    return false;
}

static bool UI_PointerBlockedByModal(frameDef_t const *frame) {
    return active_modal && !UI_FrameWithinRoot(active_modal, frame);
}

static bool UI_FrameInDrawOrder(frameDef_t const *const *draw_order, uint32_t count, frameDef_t const *frame) {
    if (!frame) {
        return false;
    }
    FOR_LOOP(i, count) {
        if (draw_order[i] == frame) {
            return true;
        }
    }
    return false;
}

static void UI_SanitizeInteractionState(frameDef_t const *const *draw_order, uint32_t count) {
    if (!draw_order || count == 0) {
        active_popup = NULL;
        UI_ResetPopupScroll();
        active_slider = NULL;
        UI_FocusEdit(NULL);
        return;
    }

    if (active_popup && !UI_FrameInDrawOrder(draw_order, count, active_popup)) {
        active_popup = NULL;
        UI_ResetPopupScroll();
    }
    if (active_popup && active_modal && !UI_FrameWithinRoot(active_modal, active_popup)) {
        active_popup = NULL;
        UI_ResetPopupScroll();
    }
    if (active_slider && !UI_FrameInDrawOrder(draw_order, count, active_slider)) {
        active_slider = NULL;
    }
    if (active_slider && active_modal && !UI_FrameWithinRoot(active_modal, active_slider)) {
        active_slider = NULL;
    }
    if (active_edit && !UI_FrameInDrawOrder(draw_order, count, active_edit)) {
        UI_FocusEdit(NULL);
    }
    if (active_edit && active_modal && !UI_FrameWithinRoot(active_modal, active_edit)) {
        UI_FocusEdit(NULL);
    }
}

static frameDef_t const *UI_FindActiveModalRoot(frameDef_t const *const *roots, uint32_t num_roots) {
    frameDef_t const *modal = NULL;

    FOR_LOOP(i, num_roots) {
        frameDef_t const *frame = roots[i];
        if (frame && !frame->hidden && frame->Type == FT_DIALOG) {
            modal = frame;
        }
    }
    return modal;
}

static uint32_t UI_FrameDrawOrderIndex(frameDef_t const *const *draw_order, uint32_t count, frameDef_t const *frame) {
    FOR_LOOP(i, count) {
        if (draw_order[i] == frame) {
            return i;
        }
    }
    return count;
}

static void UI_DrawModalDim(void) {
    refExport_t *renderer = mi.GetRenderer();
    uint32_t texture;
    texture_t const *tex;
    rect_t const *rect;

    if (!active_modal || !renderer || !renderer->DrawImageEx) {
        return;
    }

    texture = UI_LoadTexture("Textures\\Black32.blp", false);
    tex = UI_GetTexture(texture);
    if (!tex) {
        return;
    }

    rect = UI_LayoutRect(active_modal);
    if (!rect) {
        rect = &scene_rect;
    }

    renderer->DrawImageEx(&MAKE(drawImage_t,
                                .texture = tex,
                                .shader = SHADER_UI,
                                .alphamode = BLEND_MODE_BLEND,
                                .screen = *rect,
                                .uv = MAKE(rect_t, 0, 0, 1, 1),
                                .color = MAKE(color32_t, 255, 255, 255, 128)));
}

static void UI_DrawHighlightFrame(frameDef_t const *frame, rect_t const *rect) {
    refExport_t *renderer = mi.GetRenderer();

    if (!frame || !frame->Highlight.AlphaFile) {
        return;
    }
    if (!renderer || !renderer->DrawImageEx) {
        return;
    }

    texture_t const *tex = UI_GetTexture(frame->Highlight.AlphaFile);
    if (!tex) {
        return;
    }

    renderer->DrawImageEx(&MAKE(drawImage_t,
                                .texture = tex,
                                .shader = SHADER_UI,
                                .alphamode = frame->Highlight.AlphaMode,
                                .screen = *rect,
                                .uv = MAKE(rect_t, 0, 0, 1, 1),
                                .color = COLOR32_WHITE));
}

static void UI_DrawButtonHighlight(frameDef_t const *frame) {
    rect_t const *rect;
    frameDef_t const *highlight;

    if (!frame || !UI_ButtonEnabled(frame)) {
        return;
    }
    if (UI_PointerBlockedByPopup(frame)) {
        return;
    }
    rect = UI_LayoutRect(frame);
    if (!rect || !(frame->ui_flags & UIFLAG_HOVERED)) {
        return;
    }

    highlight = UI_ButtonMouseOverHighlight(frame);
    UI_DrawHighlightFrame(highlight, rect);
}

static bool UI_RenderIsButtonFrameType(FRAMETYPE type) {
    return type == FT_BUTTON ||
           type == FT_TEXTBUTTON ||
           type == FT_GLUETEXTBUTTON ||
           type == FT_GLUEBUTTON ||
           type == FT_GLUEPOPUPMENU ||
           type == FT_POPUPMENU ||
           type == FT_SIMPLEBUTTON;
}

static bool UI_RenderIsCheckBoxFrameType(FRAMETYPE type) {
    return type == FT_CHECKBOX ||
           type == FT_GLUECHECKBOX ||
           type == FT_SIMPLECHECKBOX;
}

static void UI_DrawPortrait(frameDef_t const *frame, rect_t const *rect) {
    refExport_t *renderer = mi.GetRenderer();

    if (!frame->Portrait.model) {
        return;
    }

    if (!renderer || !renderer->RenderFrame) {
        return;
    }

    model_t const *model = UI_GetModel(frame->Portrait.model);
    if (!model) {
        return;
    }

    renderEntity_t entity = {0};
    entity.model = model;
    entity.scale = 1.0f;
    entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_PORTRAIT_LIGHTING;
    renderer->SetEntityAnimFrame(model, "Stand", &entity);

    viewDef_t viewdef = {0};
    viewdef.viewport = *rect;
    viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_USE_ENTITY_CAMERA;
    viewdef.num_entities = 1;
    viewdef.entities = &entity;

    renderer->RenderFrame(&viewdef);
}

/* Render a live game-unit portrait (cl.portraits[index]) inside the given
 * frame's rect — used for the cinematic transmission portrait, whose model is a
 * game configstring index, not a UI-cache model. Mirrors UI_LayoutDrawPortrait. */
static void UI_DrawSprite(frameDef_t const *frame, rect_t const *rect) {
    refExport_t *renderer = mi.GetRenderer();
    float x = rect->x;

    if (frame->Texture.Image) {
        UI_DrawTexture(frame, rect);
        return;
    }

    if (!frame->Portrait.model) {
        return;
    }

    if (!renderer || !renderer->DrawSprite) {
        return;
    }

    model_t const *model = UI_GetModel(frame->Portrait.model);
    if (!model) {
        return;
    }
    
    cstring_t anim = (frame->Text && *frame->Text) ? frame->Text : "Stand";
    /* #! sprite sequences contain authored screen coordinates.  A fullscreen
     * 4:3 sprite keeps that geometry; center it inside the expanded canvas
     * instead of pretending that enlarging its FDF frame scales the MDX. */
    if (anim && anim[0] == '#' && anim[1] == '!' && rect->w > UI_BASE_WIDTH)
        x += (rect->w - UI_BASE_WIDTH) * 0.5f;
    renderer->DrawSprite(&MAKE(drawSprite_t, .model = model, .anim = anim, .x = x, .y = rect->y, .id = frame));
}

static void UI_DrawFrameOne(frameDef_t const *frame) {
    frameDef_t const *dialog_backdrop;

    if (!frame) {
        return;
    }
    
    /* Skip hidden frames */
    if (frame->hidden) {
        return;
    }
    
    /* Calculate layout */
    rect_t const *rect = UI_LayoutRect(frame);
    if (!rect || ((rect->w <= 0 || rect->h <= 0) && frame->Type != FT_SPRITE)) {
        return;
    }
    
    /* Render based on frame type */
    switch (frame->Type) {
        case FT_FRAME:
        case FT_SIMPLEFRAME:
            if (frame->draw) {
                frame->draw((frameDef_t *)frame, rect);
            }
            break;

        case FT_DIALOG:
            dialog_backdrop = frame->DialogBackdrop;
            if (!dialog_backdrop && frame->DialogBackdropName[0]) {
                dialog_backdrop = UI_FindChildFrame((frameDef_t *)frame, frame->DialogBackdropName);
            }
            UI_DrawBackdropWithColor(dialog_backdrop, rect, frame->Color);
            break;

        case FT_CONTROL:
            if (frame->Control.Backdrop.Normal[0]) {
                frameDef_t const *backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
                UI_DrawBackdropWithColor(backdrop, rect, frame->Color);
            }
            UI_DrawMapListControl(frame, rect);
            break;
            
        case FT_BACKDROP:
            UI_DrawBackdrop(frame, rect);
            break;
            
        case FT_TEXTURE:
            UI_DrawTexture(frame, rect);
            break;
            
        case FT_TEXT:
        case FT_STRING:
            UI_DrawText(frame, rect);
            break;
            
        case FT_BUTTON:
        case FT_TEXTBUTTON:
        case FT_GLUETEXTBUTTON:
        case FT_GLUEBUTTON:
        case FT_SIMPLEBUTTON:
        case FT_COMMANDBUTTON:
        case FT_CHECKBOX:
        case FT_GLUECHECKBOX:
        case FT_SIMPLECHECKBOX:
        case FT_SLIDER:
        case FT_EDITBOX:
        case FT_GLUEEDITBOX:
        case FT_SLASHCHATBOX:
        case FT_MENU:
            if (frame->draw) {
                frame->draw((frameDef_t *)frame, rect);
            }
            break;

        case FT_GLUEPOPUPMENU:
        case FT_POPUPMENU:
            /* Draw button background */
            {
                frameDef_t const *backdrop = UI_ButtonBackdrop(frame, rect);
                if (backdrop && backdrop->Type == FT_TEXTURE) {
                    UI_DrawTexture(backdrop, rect);
                } else {
                    UI_DrawBackdropWithColor(backdrop, rect, frame->Color);
                }
            }
            UI_DrawTexture(frame, rect);
            break;

        case FT_MODEL:
            UI_DrawPortrait(frame, rect);
            break;

        case FT_SPRITE:
            UI_DrawSprite(frame, rect);
            break;

        case FT_LISTBOX:
        case FT_TEXTAREA:
            /* TODO: Implement complex control rendering */
            break;
            
        default:
            break;
    }
    
}

/* ========================================================================
 * PUBLIC API
 * ======================================================================== */

static void UI_DrawFrameRangeSprites(frameDef_t const *const *draw_order, uint32_t start, uint32_t end) {
    for (uint32_t i = start; i < end; i++) {
        if (draw_order[i]->Type == FT_SPRITE &&
            !UI_IsActivePopupMenu(draw_order[i])) {
            UI_DrawFrameOne(draw_order[i]);
        }
    }
}

static void UI_DrawFrameRangeControls(frameDef_t const *const *draw_order, uint32_t start, uint32_t end) {
    for (uint32_t i = start; i < end; i++) {
        if (draw_order[i]->Type != FT_SPRITE &&
            !UI_IsActivePopupMenu(draw_order[i])) {
            UI_DrawFrameOne(draw_order[i]);
        }
    }
}

static void UI_DrawFrameRangeHighlights(frameDef_t const *const *draw_order, uint32_t start, uint32_t end) {
    for (uint32_t i = start; i < end; i++) {
        if (UI_RenderIsButtonFrameType(draw_order[i]->Type) &&
            !UI_IsActivePopupMenu(draw_order[i])) {
            UI_DrawButtonHighlight(draw_order[i]);
        }
        if (UI_RenderIsCheckBoxFrameType(draw_order[i]->Type) &&
            !UI_IsActivePopupMenu(draw_order[i])) {
            UI_DrawCheckBoxMouseOverHighlight(draw_order[i]);
        }
    }
}

void UI_TogglePopup(frameDef_t const *frame) {
    if (!frame) {
        return;
    }
    if (active_popup == frame) {
        UI_ResetPopupScroll();
        active_popup = NULL;
    } else {
        UI_ResetPopupScroll();
        active_popup = frame;
    }
}

/* ========================================================================
 * EVENT-TIME CONTROL INTERACTION — called from UI_MouseEventLocal
 * ======================================================================== */

void UI_SliderBeginDrag(frameDef_t const *frame, float fdf_x, float fdf_y) {
    if (!frame || frame->Type != FT_SLIDER) {
        return;
    }
    rect_t const *rect = UI_LayoutRect(frame);
    if (!rect) {
        return;
    }
    frameDef_t const *thumb = UI_FindFrameNear(frame, frame->Slider.ThumbButtonFrame);
    rect_t thumb_rect = UI_SliderThumbRect(frame, rect, thumb);
    if (UI_PointInRect(fdf_x, fdf_y, rect) || UI_PointInRect(fdf_x, fdf_y, &thumb_rect)) {
        vector2_t mouse = { fdf_x, fdf_y };
        active_slider = frame;
        ((frameDef_t *)frame)->Slider.InitialValue = UI_SliderValueFromMousePos(frame, rect, thumb, mouse);
    }
}

void UI_SliderUpdateDrag(frameDef_t const *frame, float fdf_x, float fdf_y) {
    if (!frame || active_slider != frame) {
        return;
    }
    rect_t const *rect = UI_LayoutRect(frame);
    if (!rect) {
        return;
    }
    frameDef_t const *thumb = UI_FindFrameNear(frame, frame->Slider.ThumbButtonFrame);
    vector2_t mouse = { fdf_x, fdf_y };
    ((frameDef_t *)frame)->Slider.InitialValue = UI_SliderValueFromMousePos(frame, rect, thumb, mouse);
}

void UI_SliderEndDrag(frameDef_t const *frame) {
    if (frame && active_slider == frame) {
        active_slider = NULL;
    }
}

bool UI_SliderIsDragging(void) {
    return active_slider != NULL;
}

frameDef_t const *UI_SliderActiveFrame(void) {
    return active_slider;
}

bool UI_HasActivePopup(void) {
    return active_popup != NULL;
}

void UI_EditboxFocusOnHit(frameDef_t const *frame) {
    if (frame && (frame->Type == FT_EDITBOX || frame->Type == FT_GLUEEDITBOX ||
                  frame->Type == FT_SLASHCHATBOX)) {
        UI_FocusEdit((frameDef_t *)frame);
    }
}

void UI_EditboxClearFocusOnMiss(void) {
    UI_FocusEdit(NULL);
}

void UI_MapListSelectRow(frameDef_t const *frame, float fdf_x, float fdf_y) {
    if (!frame || !frame->MapListControl.State) {
        return;
    }
    rect_t const *rect = UI_LayoutRect(frame);
    if (!rect) {
        return;
    }
    uiMapListControl_t const *control = &frame->MapListControl;
    uiMapListState_t *state = control->State;
    float row_height = control->RowHeight > 0 ? control->RowHeight : 0.019f;
    rect_t content = { rect->x + control->InsetX, rect->y + control->InsetY,
                     rect->w - control->InsetX * 2.0f, row_height };
    uint32_t visible_rows = control->VisibleRows ? control->VisibleRows :
        (uint32_t)((rect->h - control->InsetY * 2.0f) / row_height);
    if (visible_rows == 0 || state->count == 0) {
        return;
    }
    float row = (fdf_y - content.y) / row_height;
    uint32_t index = (uint32_t)floorf(state->visualScroll + row);
    if (row >= 0.0f && row < (float)visible_rows && index < state->count) {
        char command[128];
        snprintf(command, sizeof(command),
                 control->SelectCommand[0] ? control->SelectCommand : "menu_lan_select %u",
                 (unsigned)index);
        UI_QueueCommand(command);
    }
}

void UI_MapListScroll(frameDef_t const *frame, bool scroll_up) {
    if (!frame || !frame->MapListControl.State) {
        return;
    }
    uiMapListControl_t const *control = &frame->MapListControl;
    uiMapListState_t *state = control->State;
    uint32_t visible_rows = control->VisibleRows ? control->VisibleRows : 0;
    if (visible_rows == 0 || state->count <= visible_rows) {
        return;
    }
    uint32_t max_scroll = state->count - visible_rows;
    if (scroll_up) {
        state->scroll = state->scroll > 0 ? state->scroll - 1 : 0;
    } else if (state->scroll < max_scroll) {
        state->scroll++;
    }
}

void UI_PopupCloseOnMiss(void) {
    if (active_popup) {
        UI_ResetPopupScroll();
        active_popup = NULL;
    }
}

bool UI_PopupPointInside(float fdf_x, float fdf_y) {
    if (!active_popup) {
        return false;
    }
    rect_t const *popup_rect = UI_LayoutRect(active_popup);
    if (popup_rect && UI_PointInRect(fdf_x, fdf_y, popup_rect)) {
        return true;
    }
    frameDef_t *menu = UI_PopupMenuFrame(active_popup);
    if (menu) {
        rect_t const *menu_rect = UI_LayoutRect(menu);
        if (menu_rect && UI_PointInRect(fdf_x, fdf_y, menu_rect)) {
            return true;
        }
    }
    return false;
}

void UI_PopupMenuScroll(bool scroll_up) {
    frameDef_t *menu = active_popup ? UI_PopupMenuFrame(active_popup) : NULL;
    if (!menu || menu->hidden) {
        return;
    }
    float border = menu->Menu.Border > 0.0f ? menu->Menu.Border : 0.006f;
    float row_height = menu->Menu.Item.Height > 0.0f ? menu->Menu.Item.Height : 0.014f;
    rect_t const *rect = UI_LayoutRect(menu);
    if (!rect) {
        return;
    }
    float content_height = MAX(0.0f, rect->h - border * 2.0f);
    uint32_t visible_rows = content_height > 0.0f ? (uint32_t)floorf(content_height / row_height) : 0;
    if (visible_rows > menu->Menu.ItemCount) {
        visible_rows = menu->Menu.ItemCount;
    }
    uint32_t max_scroll = menu->Menu.ItemCount > visible_rows ? menu->Menu.ItemCount - visible_rows : 0;
    if (scroll_up) {
        active_popup_scroll = active_popup_scroll > 0 ? active_popup_scroll - 1 : 0;
    } else if (active_popup_scroll < max_scroll) {
        active_popup_scroll++;
    }
}

void UI_PopupMenuHover(float fdf_x, float fdf_y) {
    frameDef_t *menu = active_popup ? UI_PopupMenuFrame(active_popup) : NULL;
    active_popup_hover_item = -1;
    if (!menu || menu->hidden) {
        return;
    }
    float border = menu->Menu.Border > 0.0f ? menu->Menu.Border : 0.006f;
    float row_height = menu->Menu.Item.Height > 0.0f ? menu->Menu.Item.Height : 0.014f;
    rect_t const *rect = UI_LayoutRect(menu);
    if (!rect) {
        return;
    }
    float content_height = MAX(0.0f, rect->h - border * 2.0f);
    uint32_t visible_rows = content_height > 0.0f ? (uint32_t)floorf(content_height / row_height) : 0;
    if (visible_rows > menu->Menu.ItemCount) {
        visible_rows = menu->Menu.ItemCount;
    }
    FOR_LOOP(row_index, visible_rows) {
        uint32_t i = active_popup_scroll + row_index;
        rect_t row = { rect->x + border, rect->y + border + row_height * (float)row_index,
                     MAX(0.0f, rect->w - border * 2.0f), row_height };
        if (i >= menu->Menu.ItemCount) {
            break;
        }
        if (UI_PointInRect(fdf_x, fdf_y, &row)) {
            active_popup_hover_item = (int)i;
            return;
        }
    }
}

void UI_PopupSelectItem(float fdf_x, float fdf_y) {
    frameDef_t *menu = active_popup ? UI_PopupMenuFrame(active_popup) : NULL;
    if (!menu || menu->hidden) {
        return;
    }
    refExport_t *renderer = mi.GetRenderer();
    if (!renderer || !renderer->LoadFont || !renderer->DrawText) {
        return;
    }
    float border = menu->Menu.Border > 0.0f ? menu->Menu.Border : 0.006f;
    float row_height = menu->Menu.Item.Height > 0.0f ? menu->Menu.Item.Height : 0.014f;
    rect_t const *rect = UI_LayoutRect(menu);
    if (!rect) {
        return;
    }
    float content_height = MAX(0.0f, rect->h - border * 2.0f);
    uint32_t visible_rows = content_height > 0.0f ? (uint32_t)floorf(content_height / row_height) : 0;
    if (visible_rows > menu->Menu.ItemCount) {
        visible_rows = menu->Menu.ItemCount;
    }
    FOR_LOOP(row_index, visible_rows) {
        uint32_t i = active_popup_scroll + row_index;
        rect_t row = { rect->x + border, rect->y + border + row_height * (float)row_index,
                     MAX(0.0f, rect->w - border * 2.0f), row_height };
        if (i >= menu->Menu.ItemCount) {
            break;
        }
        if (UI_PointInRect(fdf_x, fdf_y, &row)) {
            frameDef_t *popup = (frameDef_t *)menu->Parent;
            frameDef_t *title = UI_IsPopupFrameType(popup ? popup->Type : FT_NONE)
                ? UI_PopupTitleTextFrame(popup) : NULL;
            char command[160];
            if (title) {
                UI_SetText(title, "%s", menu->Menu.Items[i].text);
            }
            if (menu->OnClick[0]) {
                snprintf(command, sizeof(command), "%s %u", menu->OnClick, (unsigned)i);
                UI_QueueCommand(command);
            }
            active_popup = NULL;
            UI_ResetPopupScroll();
            return;
        }
    }
}

void UI_DrawFramesInScene(frameDef_t const *const *roots, uint32_t num_roots, rect_t const *scene) {
    frameDef_t const *draw_order[MAX_UI_CLASSES];
    uint32_t total;
    uint32_t count;
    uint32_t modal_index;
    frameDef_t *popup_menu;

    if (!roots || num_roots == 0) {
        return;
    }
    
    /* Clear layout cache (scene rect may have changed) */
    scene_rect_valid = false;
    memset(runtimes, 0, sizeof(runtimes));
    
    /* Initialize scene rect */
    animate_frames = scene == NULL;
    scene_rect = scene ? *scene : UI_GetSceneRect();
    scene_rect_valid = true;
    total = 0;
    FOR_LOOP(i, num_roots) {
        uint32_t emitted;
        if (!roots[i] || roots[i]->hidden) {
            continue;
        }
        emitted = UI_CollectFrameTree(roots[i],
                                      total < MAX_UI_CLASSES ? draw_order + total : NULL,
                                      total < MAX_UI_CLASSES ? MAX_UI_CLASSES - total : 0);
        total += emitted;
    }
    /* The installed screen remains drawable while leaving; installation and tab
     * configuration occur centrally at the boundary before the incoming motion. */
    count = MIN(total, MAX_UI_CLASSES);
    active_modal = UI_FindActiveModalRoot(roots, num_roots);
    modal_index = UI_FrameDrawOrderIndex(draw_order, count, active_modal);
    UI_SanitizeInteractionState(draw_order, count);
    UI_UpdatePopupVisibility(draw_order, count);

    /* Match the old client overlay pass: animated/model sprites first, then
     * regular UI controls above them. */
    UI_DrawFrameRangeSprites(draw_order, 0, modal_index);
    UI_DrawFrameRangeControls(draw_order, 0, modal_index);
    UI_DrawFrameRangeHighlights(draw_order, 0, modal_index);
    if (active_modal) {
        UI_DrawModalDim();
    }
    UI_DrawFrameRangeSprites(draw_order, modal_index, count);
    UI_DrawFrameRangeControls(draw_order, modal_index, count);
    UI_DrawFrameRangeHighlights(draw_order, modal_index, count);

    popup_menu = active_popup ? UI_PopupMenuFrame(active_popup) : NULL;
    if (popup_menu && !popup_menu->hidden) {
        UI_DrawFrameOne(popup_menu);
    }
}

void UI_DrawFrames(frameDef_t const *const *roots, uint32_t num_roots) {
    UI_DrawFramesInScene(roots, num_roots, NULL);
}

void UI_DrawFrame(frameDef_t const *frame) {
    UI_DrawFrameInScene(frame, NULL);
}

void UI_DrawFrameInScene(frameDef_t const *frame, rect_t const *scene) {
    UI_DrawFramesInScene(&frame, 1, scene);
}
