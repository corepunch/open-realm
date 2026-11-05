#include "../cmath3.h"
#include <string.h>

vec3_t Box3_Center(box3_t const *box) {
    return (vec3_t) {
        (box->min.x + box->max.x) * 0.5f,
        (box->min.y + box->max.y) * 0.5f,
        (box->min.z + box->max.z) * 0.5f,
    };
}

void Box3_IncludePoint(box3_t *box, vec3_t const *point) {
    float lower[3], upper[3], p[3];
    memcpy(lower, &box->min, sizeof(lower)); memcpy(upper, &box->max, sizeof(upper));
    memcpy(p, point, sizeof(p));
    for (int i = 0; i < 3; i++) { lower[i] = fminf(lower[i], p[i]); upper[i] = fmaxf(upper[i], p[i]); }
    memcpy(&box->min, lower, sizeof(lower)); memcpy(&box->max, upper, sizeof(upper));
}
