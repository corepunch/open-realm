#ifndef box2_h
#define box2_h

#include "vector2.h"

struct box2 {
    vec2_t min;
    vec2_t max;
};

typedef struct box2 box2_t;



vec2_t Box2_center(box2_t const *box2);
void Box2_moveTo(box2_t *box, vec2_t const *newCenterLoc);
int Box2_containsPoint(box2_t const *box, vec2_t const *point);

#endif
