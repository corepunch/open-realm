#ifndef SC2_SHADOW_H
#define SC2_SHADOW_H

#include "common/common.h"

typedef struct SC2SHADOWVIEW {
    MATRIX4 camera;
    VECTOR3 target, light;
    FLOAT reach;
} SC2SHADOWVIEW;
typedef struct SC2SHADOWVIEW *LPSC2SHADOWVIEW;
typedef const struct SC2SHADOWVIEW *LPCSC2SHADOWVIEW;

/* Fit native SC2 units to the visible ground footprint, rather than a WC3-sized 3000-unit square. */
static BOOL sc2_shadow_matrix(LPCSC2SHADOWVIEW in, LPMATRIX4 out) {
    MATRIX4 inv, view, proj;
    VECTOR3 dir = in->light;
    FLOAT radius = 0;
    Matrix4_inverse(&in->camera, &inv);
    FOR_LOOP(i, 4) {
        VECTOR3 a = Matrix4_multiply_vector3(&inv, &(VECTOR3){ i & 1 ? 1 : -1, i & 2 ? 1 : -1, -1 });
        VECTOR3 b = Matrix4_multiply_vector3(&inv, &(VECTOR3){ i & 1 ? 1 : -1, i & 2 ? 1 : -1, 1 });
        VECTOR3 ray = Vector3_sub(&b, &a);
        if (fabsf(ray.z) < 0.000001f) return false;
        VECTOR3 ground = Vector3_mad(&a, (in->target.z - a.z) / ray.z, &ray);
        radius = MAX(radius, Vector3_distance(&ground, &in->target));
    }
    if (radius <= 0 || Vector3_lengthsq(&dir) <= 0 || in->reach <= 0) return false;
    Vector3_normalize(&dir);
    VECTOR3 eye = Vector3_mad(&in->target, -in->reach, &dir);
    /* A vertical sun needs a non-parallel up vector to define its light basis. */
    VECTOR3 up = fabsf(dir.z) > 0.99f ? (VECTOR3){ 0, 1, 0 } : (VECTOR3){ 0, 0, 1 };
    Matrix4_lookAt(&view, &eye, &dir, &up);
    Matrix4_ortho(&proj, -radius, radius, -radius, radius, -radius, 2 * in->reach + radius);
    Matrix4_multiply(&proj, &view, out);
    return true;
}

#endif
