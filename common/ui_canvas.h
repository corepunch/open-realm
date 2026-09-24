#ifndef UI_CANVAS_H
#define UI_CANVAS_H

#include "common/ui_constants.h"

/* One coordinate contract for layout, renderer projection and pointer input. */
static inline FLOAT UI_CanvasWidth(size2_t size) {
    if (!UI_STRETCH_CANVAS && size.height > 0) {
        FLOAT aspect = (FLOAT)size.width / (FLOAT)size.height;
        if (aspect > UI_MIN_ASPECT) return UI_BASE_HEIGHT * aspect;
    }
    return UI_BASE_WIDTH;
}

#endif
