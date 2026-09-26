#ifndef triangle3_h
#define triangle3_h

#include "vector3.h"

struct triangle3 {
    vec3_t a;
    vec3_t b;
    vec3_t c;
};

typedef struct triangle3 triangle3_t;



vec3_t Triangle_normal(triangle3_t const *triangle);

#endif
