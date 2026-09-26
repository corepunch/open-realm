#ifndef plane3_h
#define plane3_h

#include "vector3.h"

struct plane3 {
    union {
        struct { vec3_t normal; float distance; };
        struct { float a, b, c, d; };
        float v[4];
    };
};

typedef struct plane3 plane3_t;



void Plane3_Normalize(plane3_t *plane);
float Plane3_MultiplyVector3(plane3_t const *plane, vec3_t const *point);

#endif
