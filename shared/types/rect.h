#ifndef rect_h
#define rect_h

#include "vector2.h"

typedef struct rect {
    float x, y, w, h;
} rect_t;

int Rect_contains(rect_t const *rect, vector2_t const * point);
rect_t Rect_scale(rect_t const *rect, float scale);
rect_t Rect_div(rect_t const *rect, int res);
rect_t Rect_inset(rect_t const *rect, float inset);
vector2_t Rect_center(rect_t const * rect);

#endif /* rect_h */
