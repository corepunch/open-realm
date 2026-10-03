#ifndef WC3_PATHING_WIDGET_H
#define WC3_PATHING_WIDGET_H

#include "wc3_math.h"
#include <math.h>

/* Existing engine texture orientation policy. The authored cardinal pair is
 * retail-verified; arbitrary-angle raster selection remains MAP-02/FOOT. */
static inline unsigned wc3_widget_texture_turn(float angle, unsigned width, unsigned height) {
    int quarter=(width!=height)+(int)lroundf(angle/0x1.921fb6p0f);
    return ((quarter%4)+4)%4;
}

/* Original04c330: one fine cell below the upper world edge. The scalar32
 * comes from CRT initializer001c70; lower-edge equality preserves the input. */
static inline float wc3_widget_clamp_axis(float value, float minimum, float maximum) {
    float upper=wc3_sub(maximum,32.f);
    if(value<minimum) return minimum;
    if(value>upper) return upper;
    return value;
}

/* Original22f410, after the caller's map-bound clamp: truncate to the absolute
 * 64-world grid, then apply the
 * rotated texture extent's parity offsets. This is not nearest-cell rounding. */
static inline float wc3_widget_snap_axis(float value, unsigned extent) {
    int32_t whole=(int32_t)wc3_int_bits(wc3_float_bits(value));
    uint32_t aligned=(uint32_t)(whole-whole%64);
    uint32_t half=((extent>>1)&1u)*32;
    bool negative=value<0;
    aligned+=negative ? 0u-half : half;
    float result=wc3_float(wc3_from_int(aligned));
    if(extent&1u) result=wc3_add(result,negative ? -16.f : 16.f);
    return result;
}

static inline void wc3_widget_snap(float point[2], unsigned width, unsigned height) {
    point[0]=wc3_widget_snap_axis(point[0],width);
    point[1]=wc3_widget_snap_axis(point[1],height);
}

#endif
