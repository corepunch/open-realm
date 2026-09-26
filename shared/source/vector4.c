#include "../cmath3.h"

void Vector4_set(vector4_t* v, float x, float y, float z, float w) {
    v->x = x;
    v->y = y;
    v->z = z;
    v->w = w;
}

vector4_t Vector4_scale(vector4_t const *v, float s) {
    return (vector4_t) {
        .x = v->x * s,
        .y = v->y * s,
        .z = v->z * s,
        .w = v->w * s
    };
}

vector4_t Vector4_add(vector4_t const *a, vector4_t const *b) {
    return (vector4_t) {
        .x = a->x + b->x,
        .y = a->y + b->y,
        .z = a->z + b->z,
        .w = a->w + b->w
    };
}

vector4_t Vector4_unm(vector4_t const *v) {
    return (vector4_t) {
        .x = -v->x,
        .y = -v->y,
        .z = -v->z,
        .w = -v->w
    };
}
