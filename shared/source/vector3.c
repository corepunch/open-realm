#include "../cmath3.h"

float Vector3_dot(vec3_t const *a,vec3_t const *b) {
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

float Vector3_lengthsq(vec3_t const *vec) {
    return Vector3_dot(vec, vec);
}

float Vector3_len(vec3_t const *vec) {
    return sqrtf(Vector3_lengthsq(vec));
}

vec3_t Vector3_bezier(vec3_t const *a, vec3_t const *b, vec3_t const *c, vec3_t const *d, float t) {
    float const inverseFactor = 1 - t;
    float const inverseFactorTimesTwo = inverseFactor * inverseFactor;
    float const factorTimes2 = t * t;
    float const factor1 = inverseFactorTimesTwo * inverseFactor;
    float const factor2 = 3 * t * inverseFactorTimesTwo;
    float const factor3 = 3 * factorTimes2 * inverseFactor;
    float const factor4 = factorTimes2 * t;

    return (vec3_t) {
        a->x * factor1 + b->x * factor2 + c->x * factor3 + d->x * factor4,
        a->y * factor1 + b->y * factor2 + c->y * factor3 + d->y * factor4,
        a->z * factor1 + b->z * factor2 + c->z * factor3 + d->z * factor4,
    };
}

vec3_t Vector3_hermite(vec3_t const *a, vec3_t const *b, vec3_t const *c, vec3_t const *d, float t) {
    float const factorTimes2 = t * t;
    float const factor1 = factorTimes2 * (2 * t - 3) + 1;
    float const factor2 = factorTimes2 * (t - 2) + t;
    float const factor3 = factorTimes2 * (t - 1);
    float const factor4 = factorTimes2 * (3 - 2 * t);

    return (vec3_t) {
        a->x * factor1 + b->x * factor2 + c->x * factor3 + d->x * factor4,
        a->y * factor1 + b->y * factor2 + c->y * factor3 + d->y * factor4,
        a->z * factor1 + b->z * factor2 + c->z * factor3 + d->z * factor4,
    };
}

vec3_t Vector3_lerp(vec3_t const *a, vec3_t const *b, float t) {
    return (vec3_t) {
        .x = a->x * (1 - t) + b->x * t,
        .y = a->y * (1 - t) + b->y * t,
        .z = a->z * (1 - t) + b->z * t
    };
}

vec3_t Vector3_cross(vec3_t const *a, vec3_t const *b) {
    return (vec3_t) {
        .x = a->y * b->z - a->z * b->y,
        .y = a->z * b->x - a->x * b->z,
        .z = a->x * b->y - a->y * b->x
    };
}

vec3_t Vector3_sub(vec3_t const *a, vec3_t const *b) {
    return (vec3_t) {
        .x = a->x - b->x,
        .y = a->y - b->y,
        .z = a->z - b->z
    };
}

vec3_t Vector3_add(vec3_t const *a, vec3_t const *b) {
    return (vec3_t) {
        .x = a->x + b->x,
        .y = a->y + b->y,
        .z = a->z + b->z
    };
}

vec3_t Vector3_mad(vec3_t const *v, float s, vec3_t const *b) {
    return (vec3_t) {
        .x = v->x + b->x * s,
        .y = v->y + b->y * s,
        .z = v->z + b->z * s
    };
}

vec3_t Vector3_mul(vec3_t const *a, vec3_t const *b) {
    return (vec3_t) {
        .x = a->x * b->x,
        .y = a->y * b->y,
        .z = a->z * b->z
    };
}

vec3_t Vector3_scale(vec3_t const *v, float s) {
    return (vec3_t) {
        .x = v->x * s,
        .y = v->y * s,
        .z = v->z * s
    };
}

vec3_t Vector3_rotateAroundAxis(vec3_t const *v, vec3_t const *axis, float radians) {
    vec3_t n = *axis;
    float c = cosf(radians);
    float s = sinf(radians);
    float d;
    vec3_t cross;

    Vector3_normalize(&n);
    d = Vector3_dot(&n, v);
    cross = Vector3_cross(&n, v);

    return (vec3_t) {
        v->x * c + cross.x * s + n.x * d * (1.0f - c),
        v->y * c + cross.y * s + n.y * d * (1.0f - c),
        v->z * c + cross.z * s + n.z * d * (1.0f - c),
    };
}

void Vector3_normalize(vec3_t *v) {
    *v = Vector3_scale(v, 1 / Vector3_len(v));
}

void Vector3_set(vec3_t *v, float x, float y, float z) {
    v->x = x;
    v->y = y;
    v->z = z;
}

void Vector3_clear(vec3_t *v) {
    Vector3_set(v, 0, 0, 0);
}

vec3_t Vector3_unm(vec3_t const* v) {
    return (vec3_t) {
        .x = -v->x,
        .y = -v->y,
        .z = -v->z
    };
}

vec3_t Vector3_clamp01(vec3_t const *v) {
    float x = v->x > 1.0f ? 1.0f : v->x < 0.0f ? 0.0f : v->x;
    float y = v->y > 1.0f ? 1.0f : v->y < 0.0f ? 0.0f : v->y;
    float z = v->z > 1.0f ? 1.0f : v->z < 0.0f ? 0.0f : v->z;
    return (vec3_t){ x, y, z };
}

float Vector3_distance(vec3_t const *a, vec3_t const *b) {
    vec3_t const dist = Vector3_sub(a, b);
    return Vector3_len(&dist);
}
