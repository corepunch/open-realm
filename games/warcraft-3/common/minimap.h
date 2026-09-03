#ifndef WC3_MINIMAP_H
#define WC3_MINIMAP_H

#include "common/shared.h"

/* Warcraft draws the authored minimap texture across the full square frame,
 * but projects world-space content through a centred aspect-preserving area.
 * This matches Warsmash's minimapFilledArea contract for rectangular maps. */
static inline RECT WC3_MinimapContentRect(LPCRECT frame, LPCVECTOR2 map_size) {
    RECT content = frame ? *frame : (RECT){ 0 };
    FLOAT world_size;

    if (!frame || !map_size || map_size->x <= 0.0f || map_size->y <= 0.0f) {
        return content;
    }

    world_size = MAX(map_size->x, map_size->y);
    content.w = frame->w * (map_size->x / world_size);
    content.h = frame->h * (map_size->y / world_size);
    content.x = frame->x + (frame->w - content.w) * 0.5f;
    content.y = frame->y + (frame->h - content.h) * 0.5f;
    return content;
}

#endif
