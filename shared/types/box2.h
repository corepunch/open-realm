#ifndef box2_h
#define box2_h

#include "vector2.h"

struct box2 {
    vector2_t min;
    vector2_t max;
};

typedef struct box2 box2_t;



vector2_t Box2_center(box2_t const *box2);
void Box2_moveTo(box2_t *box, vector2_t const *newCenterLoc);
int Box2_containsPoint(box2_t const *box, vector2_t const *point);

#endif
