#ifndef UI_CANVAS_H
#define UI_CANVAS_H

#include "common/shared.h"
#include "common/ui_constants.h"

/* One resolver for renderer projection, client layout/pointer coordinates and glue scenes.  Aspects at or
 * below UI_MIN_ASPECT keep the authored scene (narrower windows squash it, as retail does); wider windows
 * either keep stretching it (UI_CANVAS_STRETCH) or widen it to UI_BASE_HEIGHT * aspect.  The HUD root
 * only differs from the scene under UI_CANVAS_EXPAND_CENTER, and the chrome class is wide exactly when
 * scene area exists beside that root, so a game never authors extension chrome nobody can see. */
static inline UICANVAS UI_ResolveCanvas(size2_t window, UICANVASPOLICY policy) {
    UICANVAS canvas = { .scene = { 0, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT }, .window = window, .policy = policy };
    FLOAT aspect = window.height ? (FLOAT)window.width / (FLOAT)window.height : 0.0f;
    if (policy != UI_CANVAS_STRETCH && aspect > UI_MIN_ASPECT) canvas.scene.w = UI_BASE_HEIGHT * aspect;
    canvas.root = canvas.scene;
    if (policy == UI_CANVAS_EXPAND_CENTER && canvas.scene.w > UI_BASE_WIDTH)
        canvas.root = MAKE(RECT, (canvas.scene.w - UI_BASE_WIDTH) * 0.5f, 0, UI_BASE_WIDTH, UI_BASE_HEIGHT);
    canvas.chrome = canvas.scene.w > canvas.root.w ? UI_CANVAS_WIDE : UI_CANVAS_STANDARD;
    return canvas;
}

#endif
