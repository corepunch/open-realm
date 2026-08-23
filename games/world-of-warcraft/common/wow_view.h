#ifndef WOW_VIEW_H
#define WOW_VIEW_H

#include "common/shared.h"

/* WoW camera interpolation wraps in degrees; linear interpolation would spin the long way across 0/360. */
static FLOAT Wow_LerpDegrees(FLOAT a, FLOAT b, FLOAT t) {
    FLOAT delta = fmodf(b - a, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    else if (delta < -180.0f) delta += 360.0f;
    return a + delta * t;
}

/* WoW viewangles use pitch/yaw degrees and a Z-up world in both client and renderer. */
static VECTOR3 Wow_ViewForward(LPCVECTOR3 angles) {
    FLOAT yaw = (FLOAT)DEG2RAD(angles->y), pitch = (FLOAT)DEG2RAD(angles->x);
    return (VECTOR3){ cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), -sinf(pitch) };
}

#endif
