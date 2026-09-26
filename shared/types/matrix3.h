#ifndef matrix3_h
#define matrix3_h

#include "vector3.h"

struct matrix3 {
    union {
        float v[9];
        vector3_t column[3];
    };
};

typedef struct matrix3 matrix3_t;
typedef struct matrix4 matrix4_t;




void Matrix3_normal(matrix3_t *out, matrix4_t const *modelview);

#endif
