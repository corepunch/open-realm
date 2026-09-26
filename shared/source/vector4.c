#include "../cmath3.h"

void Vector4_set(vec4_t* v, float x, float y, float z, float w) {
    v->x = x;
    v->y = y;
    v->z = z;
    v->w = w;
}

vec4_t Vector4_scale(vec4_t const *v, float s) {
    return (vec4_t) {
        .x = v->x * s,
        .y = v->y * s,
        .z = v->z * s,
        .w = v->w * s
    };
}

vec4_t Vector4_add(vec4_t const *a, vec4_t const *b) {
    return (vec4_t) {
        .x = a->x + b->x,
        .y = a->y + b->y,
        .z = a->z + b->z,
        .w = a->w + b->w
    };
}

vec4_t Vector4_unm(vec4_t const *v) {
    return (vec4_t) {
        .x = -v->x,
        .y = -v->y,
        .z = -v->z,
        .w = -v->w
    };
}
