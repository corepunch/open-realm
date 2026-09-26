#include "../cmath3.h"

vec3_t Box3_Center(box3_t const *box) {
    return (vec3_t) {
        (box->min.x + box->max.x) * 0.5f,
        (box->min.y + box->max.y) * 0.5f,
        (box->min.z + box->max.z) * 0.5f,
    };
}
