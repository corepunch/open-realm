#ifndef WOW_VIEW_H
#define WOW_VIEW_H

#include "wow_coords.h"

/* WoW camera interpolation wraps in degrees; linear interpolation would spin the long way across 0/360. */
static FLOAT Wow_LerpDegrees(FLOAT a, FLOAT b, FLOAT t) {
    FLOAT delta = fmodf(b - a, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    else if (delta < -180.0f) delta += 360.0f;
    return a + delta * t;
}

/* Shadow callers share the same no-cull override for both cheap and terrain-adjusted bounds. */
static BOOL Wow_ShadowBoundsVisible(LPCFRUSTUM3 frustum, LPCBOX3 bounds, BOOL cull) {
    return !cull || Frustum_ContainsAABox(frustum, bounds);
}

#endif
