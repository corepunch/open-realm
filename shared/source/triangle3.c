#include "../cmath3.h"

vector3_t Triangle_normal(triangle3_t const *triangle) {
    vector3_t const tside1 = Vector3_sub(&triangle->b, &triangle->a);
    vector3_t const tside2 = Vector3_sub(&triangle->c, &triangle->a);
    return Vector3_cross(&tside1, &tside2);
}
