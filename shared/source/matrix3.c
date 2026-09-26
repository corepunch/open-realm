#include <string.h>
#include "../cmath3.h"

void Matrix3_normal(matrix3_t * out, matrix4_t const * modelview) {
    struct matrix4 inverse;
    Matrix4_inverse(modelview, &inverse);
    float const m33[9] = {
        inverse.v[0], inverse.v[1], inverse.v[2],
        inverse.v[4], inverse.v[5], inverse.v[6],
        inverse.v[8], inverse.v[9], inverse.v[10]
    };
    memcpy(out, m33, sizeof(m33));
}
