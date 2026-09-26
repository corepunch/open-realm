#include "../cmath3.h"

int Rect_contains(rect_t const * rect, LPCVECTOR2 point) {
    if (rect->x > point->x) return 0;
    if (rect->y > point->y) return 0;
    if (rect->x + rect->w <= point->x) return 0;
    if (rect->y + rect->h <= point->y) return 0;
    return 1;
}

rect_t Rect_scale(rect_t const * rect, float scale) {
    rect_t const screen = {
        .x = rect->x * scale,
        .y = rect->y * scale,
        .w = rect->w * scale,
        .h = rect->h * scale,
    };
    return screen;
}

rect_t Rect_div(rect_t const * rect, int res) {
    return Rect_scale(rect, 1.0 / res);
}

rect_t Rect_inset(rect_t const * rect, float inset) {
    return (rect_t){
        .x = rect->x + inset, .y = rect->y + inset,
        .w = rect->w - inset * 2, .h = rect->h - inset * 2,
    };
}

VECTOR2 Rect_center(rect_t const * rect) {
    VECTOR2 const center = {
        rect->x + rect->w * 0.5f,
        rect->y + rect->h * 0.5f,
    };
    return center;
}
