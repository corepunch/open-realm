#ifndef box3_h
#define box3_h

#include "vector3.h"

struct box3 {
    vector3_t min;
    vector3_t max;
};

typedef struct box3 box3_t;



vector3_t Box3_Center(box3_t const * box);

#endif
