#include "../cmath3.h"

vector2_t Box2_center(box2_t const *box) {
    return (vector2_t) {
        (box->min.x + box->max.x) * 0.5f,
        (box->min.y + box->max.y) * 0.5f,
    };
}

void Box2_moveTo(box2_t *box, vector2_t const *newCenterLoc) {
    vector2_t center = Box2_center(box);
    box->min.x += newCenterLoc->x - center.x;
    box->max.x += newCenterLoc->x - center.x;
    box->min.y += newCenterLoc->y - center.y;
    box->max.y += newCenterLoc->y - center.y;
}

int Box2_containsPoint(box2_t const *box, vector2_t const *point) {
    if (point->x < box->min.x) return 0;
    if (point->y < box->min.y) return 0;
    if (point->x >= box->max.x) return 0;
    if (point->y >= box->max.y) return 0;
    return 1;
}
