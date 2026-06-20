/*
 * ui_render.c — Frame rendering and layout solving
 *
 * This module implements:
 * 1. Layout solving: Calculate screen positions from SetPoint anchors
 * 2. Frame rendering: Draw backdrops, textures, text, portraits
 * 3. Frame hierarchy traversal: Recursively render child frames
 *
 * Ported from client/cl_scrn.c (Phase 7 consolidation)
 */

#include "ui_local.h"
#include "client/ui_text_input.h"
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

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* Runtime layout state (cached screen rects) */
typedef struct {
    RECT rect;
    BOOL calculated;
} frameRuntime_t;

static frameRuntime_t runtimes[MAX_UI_CLASSES];
static RECT scene_rect;
static BOOL scene_rect_valid = FALSE;
static LPCFRAMEDEF active_slider = NULL;
static LPCFRAMEDEF active_popup = NULL;
static LPCFRAMEDEF active_modal = NULL;
static LPFRAMEDEF active_edit = NULL;
static uiTextInput_t active_ti;

static LPRENDERER UI_GetRenderer(void) {
    if (!uiimport.GetRenderer) {
        return NULL;
    }
    return uiimport.GetRenderer();
}

static BOOL UI_FrameIndex(LPCFRAMEDEF frame, DWORD *index) {
    if (!frame || frame < frames || frame >= frames + MAX_UI_CLASSES) {
        return FALSE;
    }
    *index = (DWORD)(frame - frames);
    return TRUE;
}

static BOOL UI_TextHasLineBreak(LPCSTR text) {
    for (LPCSTR p = text ? text : ""; *p; p++) {
        if (*p == '\n' || *p == '\r') {
            return TRUE;
        }
        if (*p == '|' && (p[1] == 'n' || p[1] == 'N')) {
            return TRUE;
        }
    }
    return FALSE;
}

static LPCSTR UI_FontFile(LPCSTR name) {
    return Theme_String(name && *name ? name : "MasterFont", "Default");
}

static DWORD UI_FontPixelSize(FLOAT size) {
    return size > 0 ? (DWORD)(size * 1000.0f + 0.5f) : 13;
}

/* Forward declarations */
static LPCRECT UI_LayoutRect(LPCFRAMEDEF frame);
static void UI_DrawFrameOne(LPCFRAMEDEF frame);
static BOOL UI_FrameWithinRoot(LPCFRAMEDEF root, LPCFRAMEDEF frame);
static BOOL UI_PointerBlockedByModal(LPCFRAMEDEF frame);
static LPCFRAMEDEF UI_FindActiveModalRoot(LPCFRAMEDEF const *roots, DWORD num_roots);

/* ========================================================================
 * HIT TESTING — used by event handlers to find frames under the cursor
 * ======================================================================== */

static BOOL UI_PointInRect(FLOAT x, FLOAT y, LPCRECT rect) {
    return rect && x >= rect->x && x < rect->x + rect->w &&
           y >= rect->y && y < rect->y + rect->h;
}

static BOOL UI_FrameIsInteractive(LPCFRAMEDEF frame) {
    if (!frame || frame->hidden || frame->disabled) {
        return FALSE;
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
            return TRUE;
        case FT_FRAME: case FT_SIMPLEFRAME:
            return frame->OnClick[0] || frame->MapListControl.State != NULL;
        default:
            return FALSE;
    }
}

/* Walk the layout cache back-to-front and return the topmost interactive frame
 * at the given FDF-space coordinates. Returns NULL if nothing was hit. */
LPCFRAMEDEF UI_HitTest(FLOAT fdf_x, FLOAT fdf_y) {
    for (int i = MAX_UI_CLASSES - 1; i >= 0; i--) {
        if (!runtimes[i].calculated) {
            continue;
        }
        LPCFRAMEDEF frame = &frames[i];
        if (!frame->inuse || !UI_FrameIsInteractive(frame)) {
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

static RECT UI_GetSceneRect(void) {
    if (scene_rect_valid) {
        return scene_rect;
    }
    
    LPRENDERER renderer = UI_GetRenderer();
    size2_t window;
    FLOAT window_aspect = UI_MIN_ASPECT;
    FLOAT x_scale = 1.0f;
    FLOAT y_scale = 1.0f;

    if (!renderer || !renderer->GetWindowSize) {
        return (RECT) { 0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT };
    }

    window = renderer->GetWindowSize();

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
    
    scene_rect = (RECT) {
        .x = (UI_BASE_WIDTH - scene_w) * 0.5f,
        .y = (UI_BASE_HEIGHT - scene_h) * 0.5f,
        .w = scene_w,
        .h = scene_h
    };
    
    scene_rect_valid = TRUE;
    return scene_rect;
}

static VECTOR2 UI_GetXBounds(LPCRECT rect) {
    return (VECTOR2) { rect->x, rect->x + rect->w };
}

static VECTOR2 UI_GetYBounds(LPCRECT rect) {
    return (VECTOR2) { rect->y, rect->y + rect->h };
}

static VECTOR2 UI_GetAxisBounds(LPCRECT rect, BOOL is_x_axis) {
    return is_x_axis ? UI_GetXBounds(rect) : UI_GetYBounds(rect);
}

static FLOAT UI_NormalizeAnchorOffset(LPCFRAMEPOINT p, BOOL is_x_axis) {
    return is_x_axis ? p->offset : -p->offset;
}

static LPCRECT UI_GetRelativeRect(LPCFRAMEDEF frame, LPCFRAMEDEF relativeTo) {
    if (!relativeTo) {
        /* Anchor to root scene */
        return &scene_rect;
    }
    if (relativeTo == frame->Parent) {
        return UI_LayoutRect(frame->Parent);
    }
    return UI_LayoutRect(relativeTo);
}

static FLOAT UI_GetAnchor(LPCFRAMEDEF frame,
                         LPCFRAMEPOINT p,
                         BOOL is_x_axis)
{
    VECTOR2 b = UI_GetAxisBounds(UI_GetRelativeRect(frame, p->relativeTo), is_x_axis);
    FLOAT offset = UI_NormalizeAnchorOffset(p, is_x_axis);
    
    if (p->targetPos == FPP_MID) {
        return (b.x + b.y) / 2.0f + offset;
    } else if (p->targetPos == FPP_MAX) {
        return b.y + offset;
    } else {
        return b.x + offset;
    }
}

static VECTOR2 UI_SolveAxisPosition(LPCFRAMEDEF frame,
                                   FRAMEPOINT const *points,
                                   FLOAT size,
                                   BOOL is_x_axis)
{
    LPCFRAMEPOINT pmin = &points[FPP_MIN];
    LPCFRAMEPOINT pmid = &points[FPP_MID];
    LPCFRAMEPOINT pmax = &points[FPP_MAX];

    if (pmid->used) {
        /* Center anchor: position = mid - size/2 */
        return (VECTOR2) {
            UI_GetAnchor(frame, pmid, is_x_axis) - size / 2.0f,
            size,
        };
    } else if (pmin->used && pmax->used) {
        /* Both min and max: stretch between anchors */
        FLOAT anchor_min = UI_GetAnchor(frame, pmin, is_x_axis);
        FLOAT anchor_max = UI_GetAnchor(frame, pmax, is_x_axis);
        return (VECTOR2) {
            anchor_min,
            anchor_max - anchor_min,
        };
    } else if (pmax->used) {
        /* Max anchor only: position = max - size */
        return (VECTOR2) {
            UI_GetAnchor(frame, pmax, is_x_axis) - size,
            size,
        };
    } else if (pmin->used) {
        /* Min anchor only: position = min */
        return (VECTOR2) {
            UI_GetAnchor(frame, pmin, is_x_axis),
            size,
        };
    }
    
    /* No anchors set: default to (0,0) with given size */
    return (VECTOR2) { 0, size };
}

static LPCRECT UI_LayoutRect(LPCFRAMEDEF frame) {
    static RECT uncached_rect;
    DWORD frame_index;
    RECT *out;

    if (!frame) {
        return &scene_rect;
    }
    
    /* Check cache */
    if (UI_FrameIndex(frame, &frame_index) && runtimes[frame_index].calculated) {
        return &runtimes[frame_index].rect;
    }
    out = UI_FrameIndex(frame, &frame_index) ? &runtimes[frame_index].rect : &uncached_rect;
    
    /* Mark as calculated to prevent recursion */
    if (UI_FrameIndex(frame, &frame_index)) {
        runtimes[frame_index].calculated = TRUE;
    }
    
    /* Calculate intrinsic size based on frame type */
    FLOAT intrinsic_w = frame->Width;
    FLOAT intrinsic_h = frame->Height;
    
    if (intrinsic_w == 0 || intrinsic_h == 0) {
        /* Try to derive size from content */
        switch (frame->Type) {
            case FT_TEXT:
            case FT_STRING:
                if (frame->Text && frame->Font.Index) {
                    BOOL auto_width = intrinsic_w == 0;
                    BOOL auto_height = intrinsic_h == 0;
                    LPRENDERER renderer = UI_GetRenderer();
                    drawText_t dt = {
                        .font = renderer ? renderer->LoadFont(UI_FontFile(frame->Font.Name),
                                                              UI_FontPixelSize(frame->Font.Size)) : NULL,
                        .text = frame->Text,
                        .textWidth = intrinsic_w > 0 ? intrinsic_w : 0.0f,
                        .lineHeight = 1.0f,
                        .wordWrap = intrinsic_w > 0,
                    };
                    VECTOR2 text_size = renderer ? renderer->GetTextSize((LPCDRAWTEXT)&dt) : MAKE(VECTOR2, 0, 0);
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
                    LPRENDERER renderer = UI_GetRenderer();
                    LPCTEXTURE texture = UI_GetTexture(frame->Texture.Image);
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
            default:
                /* Default size if not specified */
                if (intrinsic_w == 0) intrinsic_w = 0.1f;
                if (intrinsic_h == 0) intrinsic_h = 0.1f;
                break;
        }
    }
    
    /* Solve X and Y positions */
    VECTOR2 x_pos = UI_SolveAxisPosition(frame, frame->Points.x, intrinsic_w, TRUE);
    VECTOR2 y_pos = UI_SolveAxisPosition(frame, frame->Points.y, intrinsic_h, FALSE);
    
    *out = (RECT) {
        .x = x_pos.x,
        .y = y_pos.x,
        .w = x_pos.y,
        .h = y_pos.y,
    };
    
    return out;
}

/* ========================================================================
 * FRAME RENDERING
 * ======================================================================== */

static void UI_DrawTexture(LPCFRAMEDEF frame, LPCRECT rect) {
    LPRENDERER renderer = UI_GetRenderer();

    if (!frame->Texture.Image) {
        return;
    }

    if (!renderer || !renderer->DrawImageEx) {
        return;
    }

    LPCTEXTURE tex = UI_GetTexture(frame->Texture.Image);
    if (!tex) {
        return;
    }
    
    /* Use TexCoord if specified, otherwise full texture */
    /* BOX2 has min/max, not x/y/w/h */
    RECT uv;
    if (frame->Texture.TexCoord.min.x != 0 || frame->Texture.TexCoord.min.y != 0 ||
        frame->Texture.TexCoord.max.x != 0 || frame->Texture.TexCoord.max.y != 0) {
        uv = (RECT) {
            frame->Texture.TexCoord.min.x,
            frame->Texture.TexCoord.min.y,
            frame->Texture.TexCoord.max.x - frame->Texture.TexCoord.min.x,
            frame->Texture.TexCoord.max.y - frame->Texture.TexCoord.min.y
        };
    } else {
        uv = (RECT) { 0, 0, 1, 1 };
    }
    
    drawImage_t di = {
        .texture = tex,
        .shader = SHADER_UI,
        .alphamode = frame->AlphaMode,
        .screen = *rect,
        .uv = uv,
        .color = frame->Color,
        .rotate = FALSE,
    };
    
    renderer->DrawImageEx((LPCDRAWIMAGE)&di);
}

static void UI_DrawText(LPCFRAMEDEF frame, LPCRECT rect) {
    LPRENDERER renderer = UI_GetRenderer();
    LPCSTR font_name;
    DWORD font_size;
    RECT text_rect = *rect;

    if (!frame->Text || !*frame->Text) {
        return;
    }

    if (!renderer || !renderer->LoadFont || !renderer->DrawText) {
        return;
    }

    font_name = UI_FontFile(frame->Font.Name);
    font_size = UI_FontPixelSize(frame->Font.Size);
    LPCFONT font = renderer->LoadFont(font_name, font_size);
    if (!font) {
        return;
    }
    
    text_rect.x += frame->Font.Justification.Offset.x;
    text_rect.y += frame->Font.Justification.Offset.y;

    COLOR32 color = frame->Font.Color;
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
        .wordWrap = frame->Width > 0,
        .halign = frame->Font.Justification.Horizontal,
        .valign = frame->Font.Justification.Vertical,
    };
    
    renderer->DrawText((LPCDRAWTEXT)&dt);
}

static void UI_DrawHighlightFrame(LPCFRAMEDEF frame, LPCRECT rect);

#include "controls/ui_control_backdrop.h"
#include "controls/ui_control_popup_menu.h"
#include "controls/ui_control_button.h"
#include "controls/ui_control_checkbox.h"
#include "controls/ui_control_editbox.h"
#include "controls/ui_control_map_list.h"
#include "controls/ui_control_slider.h"

BOOL UI_EditHasFocus(LPCFRAMEDEF frame) {
    return active_edit && active_edit == frame;
}

LPCSTR UI_EditValue(LPCFRAMEDEF frame) {
    return UI_EditText(frame);
}

void UI_SetEditValue(LPFRAMEDEF frame, LPCSTR text) {
    UI_SetEditText(frame, text);
}

void UI_ClearEditFocus(void) {
    UI_FocusEdit(NULL);
}

static BOOL UI_FrameWithinRoot(LPCFRAMEDEF root, LPCFRAMEDEF frame) {
    LPCFRAMEDEF cursor = frame;

    while (cursor) {
        if (cursor == root) {
            return TRUE;
        }
        cursor = cursor->Parent;
    }

    return FALSE;
}

static BOOL UI_PointerBlockedByModal(LPCFRAMEDEF frame) {
    return active_modal && !UI_FrameWithinRoot(active_modal, frame);
}

static BOOL UI_FrameInDrawOrder(LPCFRAMEDEF const *draw_order, DWORD count, LPCFRAMEDEF frame) {
    if (!frame) {
        return FALSE;
    }
    FOR_LOOP(i, count) {
        if (draw_order[i] == frame) {
            return TRUE;
        }
    }
    return FALSE;
}

static void UI_SanitizeInteractionState(LPCFRAMEDEF const *draw_order, DWORD count) {
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

static LPCFRAMEDEF UI_FindActiveModalRoot(LPCFRAMEDEF const *roots, DWORD num_roots) {
    LPCFRAMEDEF modal = NULL;

    FOR_LOOP(i, num_roots) {
        LPCFRAMEDEF frame = roots[i];
        if (frame && !frame->hidden && frame->Type == FT_DIALOG) {
            modal = frame;
        }
    }
    return modal;
}

static DWORD UI_FrameDrawOrderIndex(LPCFRAMEDEF const *draw_order, DWORD count, LPCFRAMEDEF frame) {
    FOR_LOOP(i, count) {
        if (draw_order[i] == frame) {
            return i;
        }
    }
    return count;
}

static void UI_DrawModalDim(void) {
    LPRENDERER renderer = UI_GetRenderer();
    DWORD texture;
    LPCTEXTURE tex;
    LPCRECT rect;

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
                                .uv = MAKE(RECT, 0, 0, 1, 1),
                                .color = MAKE(COLOR32, 255, 255, 255, 128),
                                .rotate = FALSE));
}

static void UI_DrawHighlightFrame(LPCFRAMEDEF frame, LPCRECT rect) {
    LPRENDERER renderer = UI_GetRenderer();

    if (!frame || !frame->Highlight.AlphaFile) {
        return;
    }
    if (!renderer || !renderer->DrawImageEx) {
        return;
    }

    LPCTEXTURE tex = UI_GetTexture(frame->Highlight.AlphaFile);
    if (!tex) {
        return;
    }

    renderer->DrawImageEx(&MAKE(drawImage_t,
                                .texture = tex,
                                .shader = SHADER_UI,
                                .alphamode = frame->Highlight.AlphaMode,
                                .screen = *rect,
                                .uv = MAKE(RECT, 0, 0, 1, 1),
                                .color = COLOR32_WHITE,
                                .rotate = FALSE));
}

static void UI_DrawButtonHighlight(LPCFRAMEDEF frame) {
    LPCRECT rect;
    LPCFRAMEDEF highlight;

    if (!frame || !UI_ButtonEnabled(frame)) {
        return;
    }
    if (UI_PointerBlockedByPopup(frame)) {
        return;
    }
    rect = UI_LayoutRect(frame);
    if (!rect || !UI_MouseContains(rect)) {
        return;
    }

    highlight = UI_ButtonMouseOverHighlight(frame);
    UI_DrawHighlightFrame(highlight, rect);
}

static BOOL UI_RenderIsButtonFrameType(FRAMETYPE type) {
    return type == FT_BUTTON ||
           type == FT_TEXTBUTTON ||
           type == FT_GLUETEXTBUTTON ||
           type == FT_GLUEBUTTON ||
           type == FT_GLUEPOPUPMENU ||
           type == FT_POPUPMENU ||
           type == FT_SIMPLEBUTTON;
}

static BOOL UI_RenderIsCheckBoxFrameType(FRAMETYPE type) {
    return type == FT_CHECKBOX ||
           type == FT_GLUECHECKBOX ||
           type == FT_SIMPLECHECKBOX;
}

static void UI_DrawPortrait(LPCFRAMEDEF frame, LPCRECT rect) {
    LPRENDERER renderer = UI_GetRenderer();

    if (!frame->Portrait.model) {
        return;
    }

    if (!renderer || !renderer->RenderFrame) {
        return;
    }

    LPCMODEL model = UI_GetModel(frame->Portrait.model);
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
void UI_DrawGamePortraitInFrame(LPCFRAMEDEF frame, DWORD modelIndex, LPCSTR anim) {
    LPRENDERER renderer = UI_GetRenderer();
    LPCMODEL model;

    if (!frame || !modelIndex || !renderer || !renderer->DrawPortrait) {
        return;
    }
    LPCRECT rect = UI_LayoutRect(frame);
    if (!rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }
    model = uiimport.GetPortrait ? uiimport.GetPortrait(modelIndex) : NULL;
    if (!model) {
        model = uiimport.GetModel ? uiimport.GetModel(modelIndex) : NULL;
    }
    if (!model) {
        return;
    }
    RECT viewport = {
        rect->x / UI_BASE_WIDTH,
        (UI_BASE_HEIGHT - rect->y - rect->h) / UI_BASE_HEIGHT,
        rect->w / UI_BASE_WIDTH,
        rect->h / UI_BASE_HEIGHT,
    };
    renderer->DrawPortrait(model, &viewport, anim ? anim : "Portrait");
}

static void UI_DrawSprite(LPCFRAMEDEF frame, LPCRECT rect) {
    LPRENDERER renderer = UI_GetRenderer();

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

    LPCMODEL model = UI_GetModel(frame->Portrait.model);
    if (!model) {
        return;
    }
    
    LPCSTR anim = (frame->Text && *frame->Text) ? frame->Text : "Stand";
    renderer->DrawSprite(model, anim, rect->x, rect->y);
}

static void UI_DrawFrameOne(LPCFRAMEDEF frame) {
    LPCFRAMEDEF dialog_backdrop;

    if (!frame) {
        return;
    }
    
    /* Skip hidden frames */
    if (frame->hidden) {
        return;
    }
    
    /* Calculate layout */
    LPCRECT rect = UI_LayoutRect(frame);
    if (!rect || ((rect->w <= 0 || rect->h <= 0) && frame->Type != FT_SPRITE)) {
        return;
    }
    
    /* Render based on frame type */
    switch (frame->Type) {
        case FT_FRAME:
        case FT_SIMPLEFRAME:
            /* Container frames have no visual representation */
            if (frame->MapListControl.State) {
                UI_DrawMapListControl(frame, rect);
            }
            break;

        case FT_DIALOG:
            dialog_backdrop = frame->DialogBackdrop;
            if (!dialog_backdrop && frame->DialogBackdropName[0]) {
                dialog_backdrop = UI_FindChildFrame((LPFRAMEDEF)frame, frame->DialogBackdropName);
            }
            UI_DrawBackdropWithColor(dialog_backdrop, rect, frame->Color);
            break;

        case FT_CONTROL:
            if (frame->Control.Backdrop.Normal[0]) {
                LPCFRAMEDEF backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
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
        case FT_GLUEPOPUPMENU:
        case FT_POPUPMENU:
        case FT_SIMPLEBUTTON:
            /* Draw button background */
            {
                LPCFRAMEDEF backdrop = UI_ButtonBackdrop(frame, rect);
                if (backdrop && backdrop->Type == FT_TEXTURE) {
                    UI_DrawTexture(backdrop, rect);
                } else {
                    UI_DrawBackdropWithColor(backdrop, rect, frame->Color);
                }
            }
            UI_DrawTexture(frame, rect);
            UI_DrawButtonText(frame, rect);
            break;
            
        case FT_MODEL:
            UI_DrawPortrait(frame, rect);
            break;
            
        case FT_SPRITE:
            UI_DrawSprite(frame, rect);
            break;
            
        case FT_SLIDER:
            UI_DrawSlider(frame, rect);
            break;

        case FT_CHECKBOX:
        case FT_GLUECHECKBOX:
        case FT_SIMPLECHECKBOX:
            UI_DrawCheckBox(frame, rect);
            break;

        case FT_EDITBOX:
        case FT_GLUEEDITBOX:
        case FT_SLASHCHATBOX:
            if (!UI_PointerBlockedByPopup(frame) &&
                UI_MouseContains(rect) &&
                uiimport.GetMouseEvent && uiimport.GetMouseEvent() == UI_CLIENT_MOUSE_LEFT_DOWN) {
                UI_FocusEdit((LPFRAMEDEF)frame);
            }
            UI_DrawEditBox(frame, rect);
            break;

        case FT_MENU:
            UI_DrawMenu(frame, rect);
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

static void UI_DrawFrameRangeSprites(LPCFRAMEDEF const *draw_order, DWORD start, DWORD end) {
    for (DWORD i = start; i < end; i++) {
        if (draw_order[i]->Type == FT_SPRITE &&
            !UI_IsActivePopupMenu(draw_order[i])) {
            UI_DrawFrameOne(draw_order[i]);
        }
    }
}

static void UI_DrawFrameRangeControls(LPCFRAMEDEF const *draw_order, DWORD start, DWORD end) {
    for (DWORD i = start; i < end; i++) {
        if (draw_order[i]->Type != FT_SPRITE &&
            !UI_IsActivePopupMenu(draw_order[i])) {
            UI_DrawFrameOne(draw_order[i]);
        }
    }
}

static void UI_DrawFrameRangeHighlights(LPCFRAMEDEF const *draw_order, DWORD start, DWORD end) {
    for (DWORD i = start; i < end; i++) {
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

void UI_TogglePopup(LPCFRAMEDEF frame) {
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

void UI_DrawFrames(LPCFRAMEDEF const *roots, DWORD num_roots) {
    LPCFRAMEDEF draw_order[MAX_UI_CLASSES];
    DWORD total;
    DWORD count;
    DWORD modal_index;
    LPFRAMEDEF popup_menu;

    if (!roots || num_roots == 0) {
        return;
    }
    
    /* Clear layout cache (scene rect may have changed) */
    scene_rect_valid = FALSE;
    memset(runtimes, 0, sizeof(runtimes));
    
    /* Initialize scene rect */
    scene_rect = UI_GetSceneRect();
    total = 0;
    FOR_LOOP(i, num_roots) {
        DWORD emitted;
        if (!roots[i] || roots[i]->hidden) {
            continue;
        }
        emitted = UI_CollectFrameTree(roots[i],
                                      total < MAX_UI_CLASSES ? draw_order + total : NULL,
                                      total < MAX_UI_CLASSES ? MAX_UI_CLASSES - total : 0);
        total += emitted;
    }
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

void UI_DrawFrame(LPCFRAMEDEF frame) {
    UI_DrawFrames(&frame, 1);
}
