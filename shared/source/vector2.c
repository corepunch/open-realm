#include "../cmath3.h"

void Vector2_set(vector2_t *v, float x, float y) {
    v->x = x;
    v->y = y;
}

vector2_t Vector2_scale(vector2_t const *v, float s) {
    return (vector2_t) {
        .x = v->x * s,
        .y = v->y * s
    };
}

vector2_t Vector2_add(vector2_t const *a, vector2_t const *b) {
    return (vector2_t) {
        .x = a->x + b->x,
        .y = a->y + b->y,
    };
}

vector2_t Vector2_sub(vector2_t const *a, vector2_t const *b) {
    return (vector2_t) {
        .x = a->x - b->x,
        .y = a->y - b->y,
    };
}

vector2_t Vector2_unm(vector2_t const *v) {
    return (vector2_t) {
        .x = -v->x,
        .y = -v->y,
    };
}


float Vector2_dot(vector2_t const *a, vector2_t const *b) {
    return a->x * b->x + a->y * b->y;
}

float Vector2_lengthsq(vector2_t const *vec) {
    return Vector2_dot(vec, vec);
}

float Vector2_len(vector2_t const *vec) {
    return sqrtf(Vector2_lengthsq(vec));
}

float Vector2_distance(vector2_t const *a, vector2_t const *b) {
    vector2_t const dist = Vector2_sub(a, b);
    return Vector2_len(&dist);
}

void Vector2_normalize(vector2_t *v) {
    *v = Vector2_scale(v, 1 / Vector2_len(v));
}

vector2_t Vector2_lerp(vector2_t const *a, vector2_t const *b, float t) {
    return (vector2_t) {
        .x = a->x * (1 - t) + b->x * t,
        .y = a->y * (1 - t) + b->y * t,
    };
}

vector2_t Vector2_mad(vector2_t const *v, float s, vector2_t const *b) {
    return (vector2_t) {
        .x = v->x + b->x * s,
        .y = v->y + b->y * s,
    };
}

float LerpNumber(float a, float b, float t) {
    return a * (1 - t) + b * t;
}

