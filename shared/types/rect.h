#ifndef rect_h
#define rect_h

#include "vector2.h"

typedef struct rect {
    float x, y, w, h;
} rect_t;

int Rect_contains(rect_t const *rect, LPCVECTOR2 point);
rect_t Rect_scale(rect_t const *rect, float scale);
rect_t Rect_div(rect_t const *rect, int res);
rect_t Rect_inset(rect_t const *rect, float inset);
VECTOR2 Rect_center(rect_t const * rect);

#endif /* rect_h */
