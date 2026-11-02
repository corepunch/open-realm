#ifndef WC3_AREA_PREVIEW_H
#define WC3_AREA_PREVIEW_H

#include "common/shared.h"
#include <math.h>

/* A targeting preview is presentation, not an ability target filter. Retail
 * can highlight collision participants even when a spell cannot affect them. */
static inline bool WC3_AreaPreviewIntersects(vec2_t const *center, float radius,
                                               vec2_t const *point, float footprint) {
    float dx, dy, reach;
    if (!center || !point || !isfinite(radius) || radius <= 0.0f ||
        !isfinite(footprint) || footprint < 0.0f) return false;
    reach = radius + footprint;
    dx = point->x - center->x;
    dy = point->y - center->y;
    return dx * dx + dy * dy <= reach * reach;
}

/* A dominant green blend remains green even for scripted red/blue models.
 * The original model tint and alpha remain intact outside this draw. */
static inline color32_t WC3_AreaPreviewGreen(color32_t base) {
    return (color32_t){ (uint8_t)(base.r / 4u),
                         (uint8_t)((base.g + 3u * 255u) / 4u),
                         (uint8_t)(base.b / 4u), base.a };
}

#endif
