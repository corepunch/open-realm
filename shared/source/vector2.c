#include "../cmath3.h"

void Vector2_set(vec2_t *v, float x, float y) {
    v->x = x;
    v->y = y;
}

vec2_t Vector2_scale(vec2_t const *v, float s) {
    return (vec2_t) {
        .x = v->x * s,
        .y = v->y * s
    };
}

vec2_t Vector2_add(vec2_t const *a, vec2_t const *b) {
    return (vec2_t) {
        .x = a->x + b->x,
        .y = a->y + b->y,
    };
}

vec2_t Vector2_sub(vec2_t const *a, vec2_t const *b) {
    return (vec2_t) {
        .x = a->x - b->x,
        .y = a->y - b->y,
    };
}

vec2_t Vector2_unm(vec2_t const *v) {
    return (vec2_t) {
        .x = -v->x,
        .y = -v->y,
    };
}


float Vector2_dot(vec2_t const *a, vec2_t const *b) {
    return a->x * b->x + a->y * b->y;
}

float Vector2_lengthsq(vec2_t const *vec) {
    return Vector2_dot(vec, vec);
}

float Vector2_len(vec2_t const *vec) {
    return sqrtf(Vector2_lengthsq(vec));
}

float Vector2_distance(vec2_t const *a, vec2_t const *b) {
    vec2_t const dist = Vector2_sub(a, b);
    return Vector2_len(&dist);
}

void Vector2_normalize(vec2_t *v) {
    *v = Vector2_scale(v, 1 / Vector2_len(v));
}

vec2_t Vector2_lerp(vec2_t const *a, vec2_t const *b, float t) {
    return (vec2_t) {
        .x = a->x * (1 - t) + b->x * t,
        .y = a->y * (1 - t) + b->y * t,
    };
}

vec2_t Vector2_mad(vec2_t const *v, float s, vec2_t const *b) {
    return (vec2_t) {
        .x = v->x + b->x * s,
        .y = v->y + b->y * s,
    };
}

float LerpNumber(float a, float b, float t) {
    return a * (1 - t) + b * t;
}

