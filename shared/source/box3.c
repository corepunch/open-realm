#include "../cmath3.h"

vector3_t Box3_Center(box3_t const * box) {
    return (vector3_t) {
        (box->min.x + box->max.x) * 0.5f,
        (box->min.y + box->max.y) * 0.5f,
        (box->min.z + box->max.z) * 0.5f,
    };
}
