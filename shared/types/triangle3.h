#ifndef triangle3_h
#define triangle3_h

#include "vector3.h"

struct triangle3 {
    vector3_t a;
    vector3_t b;
    vector3_t c;
};

typedef struct triangle3 triangle3_t;



vector3_t Triangle_normal(triangle3_t const * triangle);

#endif
