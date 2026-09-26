#ifndef box3_h
#define box3_h

#include "vector3.h"

struct box3 {
    vec3_t min;
    vec3_t max;
};

typedef struct box3 box3_t;



vec3_t Box3_Center(box3_t const *box);

#endif
