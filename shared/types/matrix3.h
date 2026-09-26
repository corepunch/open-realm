#ifndef matrix3_h
#define matrix3_h

#include "vector3.h"

struct matrix3 {
    union {
        float v[9];
        vec3_t column[3];
    };
};

typedef struct matrix3 mat3_t;
typedef struct matrix4 mat4_t;




void Matrix3_normal(mat3_t *out, mat4_t const *modelview);

#endif
