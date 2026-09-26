#ifndef WOW_VIEW_H
#define WOW_VIEW_H

#include "wow_coords.h"

/* WoW camera interpolation wraps in degrees; linear interpolation would spin the long way across 0/360. */
static float Wow_LerpDegrees(float a, float b, float t) {
    float delta = fmodf(b - a, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    else if (delta < -180.0f) delta += 360.0f;
    return a + delta * t;
}

/* Shadow callers share the same no-cull override for both cheap and terrain-adjusted bounds. */
static bool Wow_ShadowBoundsVisible(frustum3_t const *frustum, box3_t const *bounds, bool cull) {
    return !cull || Frustum_ContainsAABox(frustum, bounds);
}

#endif
