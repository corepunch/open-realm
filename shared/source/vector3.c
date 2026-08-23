#include "../cmath3.h"

float Vector3_dot(LPCVECTOR3 a,LPCVECTOR3 b) {
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

float Vector3_lengthsq(LPCVECTOR3 vec) {
    return Vector3_dot(vec, vec);
}

float Vector3_len(LPCVECTOR3 vec) {
    return sqrtf(Vector3_lengthsq(vec));
}

VECTOR3 Vector3_bezier(LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c, LPCVECTOR3 d, float t) {
    float const inverseFactor = 1 - t;
    float const inverseFactorTimesTwo = inverseFactor * inverseFactor;
    float const factorTimes2 = t * t;
    float const factor1 = inverseFactorTimesTwo * inverseFactor;
    float const factor2 = 3 * t * inverseFactorTimesTwo;
    float const factor3 = 3 * factorTimes2 * inverseFactor;
    float const factor4 = factorTimes2 * t;

    return (VECTOR3) {
        a->x * factor1 + b->x * factor2 + c->x * factor3 + d->x * factor4,
        a->y * factor1 + b->y * factor2 + c->y * factor3 + d->y * factor4,
        a->z * factor1 + b->z * factor2 + c->z * factor3 + d->z * factor4,
    };
}

VECTOR3 Vector3_hermite(LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c, LPCVECTOR3 d, float t) {
    float const factorTimes2 = t * t;
    float const factor1 = factorTimes2 * (2 * t - 3) + 1;
    float const factor2 = factorTimes2 * (t - 2) + t;
    float const factor3 = factorTimes2 * (t - 1);
    float const factor4 = factorTimes2 * (3 - 2 * t);

    return (VECTOR3) {
        a->x * factor1 + b->x * factor2 + c->x * factor3 + d->x * factor4,
        a->y * factor1 + b->y * factor2 + c->y * factor3 + d->y * factor4,
        a->z * factor1 + b->z * factor2 + c->z * factor3 + d->z * factor4,
    };
}

VECTOR3 Vector3_lerp(LPCVECTOR3 a, LPCVECTOR3 b, float t) {
    return (VECTOR3) {
        .x = a->x * (1 - t) + b->x * t,
        .y = a->y * (1 - t) + b->y * t,
        .z = a->z * (1 - t) + b->z * t
    };
}

VECTOR3 Vector3_cross(LPCVECTOR3 a, LPCVECTOR3 b) {
    return (VECTOR3) {
        .x = a->y * b->z - a->z * b->y,
        .y = a->z * b->x - a->x * b->z,
        .z = a->x * b->y - a->y * b->x
    };
}

VECTOR3 Vector3_sub(LPCVECTOR3 a, LPCVECTOR3 b) {
    return (VECTOR3) {
        .x = a->x - b->x,
        .y = a->y - b->y,
        .z = a->z - b->z
    };
}

VECTOR3 Vector3_add(LPCVECTOR3 a, LPCVECTOR3 b) {
    return (VECTOR3) {
        .x = a->x + b->x,
        .y = a->y + b->y,
        .z = a->z + b->z
    };
}

VECTOR3 Vector3_mad(LPCVECTOR3 v, float s, LPCVECTOR3 b) {
    return (VECTOR3) {
        .x = v->x + b->x * s,
        .y = v->y + b->y * s,
        .z = v->z + b->z * s
    };
}

VECTOR3 Vector3_mul(LPCVECTOR3 a, LPCVECTOR3 b) {
    return (VECTOR3) {
        .x = a->x * b->x,
        .y = a->y * b->y,
        .z = a->z * b->z
    };
}

VECTOR3 Vector3_scale(LPCVECTOR3 v, float s) {
    return (VECTOR3) {
        .x = v->x * s,
        .y = v->y * s,
        .z = v->z * s
    };
}

VECTOR3 Vector3_rotateAroundAxis(LPCVECTOR3 v, LPCVECTOR3 axis, float radians) {
    VECTOR3 n = *axis;
    float c = cosf(radians);
    float s = sinf(radians);
    float d;
    VECTOR3 cross;

    Vector3_normalize(&n);
    d = Vector3_dot(&n, v);
    cross = Vector3_cross(&n, v);

    return (VECTOR3) {
        v->x * c + cross.x * s + n.x * d * (1.0f - c),
        v->y * c + cross.y * s + n.y * d * (1.0f - c),
        v->z * c + cross.z * s + n.z * d * (1.0f - c),
    };
}

void Vector3_normalize(LPVECTOR3 v) {
    *v = Vector3_scale(v, 1 / Vector3_len(v));
}

void Vector3_set(LPVECTOR3 v, float x, float y, float z) {
    v->x = x;
    v->y = y;
    v->z = z;
}

void Vector3_clear(LPVECTOR3 v) {
    Vector3_set(v, 0, 0, 0);
}

VECTOR3 Vector3_unm(VECTOR3 const* v) {
    return (VECTOR3) {
        .x = -v->x,
        .y = -v->y,
        .z = -v->z
    };
}

VECTOR3 Vector3_clamp01(LPCVECTOR3 v) {
    float x = v->x > 1.0f ? 1.0f : v->x < 0.0f ? 0.0f : v->x;
    float y = v->y > 1.0f ? 1.0f : v->y < 0.0f ? 0.0f : v->y;
    float z = v->z > 1.0f ? 1.0f : v->z < 0.0f ? 0.0f : v->z;
    return (VECTOR3){ x, y, z };
}

float Vector3_distance(LPCVECTOR3 a, LPCVECTOR3 b) {
    VECTOR3 const dist = Vector3_sub(a, b);
    return Vector3_len(&dist);
}
