#ifndef SC2_SHADOW_H
#define SC2_SHADOW_H

#include "common/common.h"

typedef struct sc2shadowview_s {
    matrix4_t camera;
    vector3_t target, light;
    float reach;
} sc2shadowview_t;



/* Fit native SC2 units to the visible ground footprint, rather than a WC3-sized 3000-unit square. */
static bool sc2_shadow_matrix(sc2shadowview_t const *in, matrix4_t *out) {
    matrix4_t inv, view, proj;
    vector3_t dir = in->light;
    float radius = 0;
    Matrix4_inverse(&in->camera, &inv);
    FOR_LOOP(i, 4) {
        vector3_t a = Matrix4_multiply_vector3(&inv, &(vector3_t){ i & 1 ? 1 : -1, i & 2 ? 1 : -1, -1 });
        vector3_t b = Matrix4_multiply_vector3(&inv, &(vector3_t){ i & 1 ? 1 : -1, i & 2 ? 1 : -1, 1 });
        vector3_t ray = Vector3_sub(&b, &a);
        if (fabsf(ray.z) < 0.000001f) return false;
        vector3_t ground = Vector3_mad(&a, (in->target.z - a.z) / ray.z, &ray);
        radius = MAX(radius, Vector3_distance(&ground, &in->target));
    }
    if (radius <= 0 || Vector3_lengthsq(&dir) <= 0 || in->reach <= 0) return false;
    Vector3_normalize(&dir);
    vector3_t eye = Vector3_mad(&in->target, -in->reach, &dir);
    /* A vertical sun needs a non-parallel up vector to define its light basis. */
    vector3_t up = fabsf(dir.z) > 0.99f ? (vector3_t){ 0, 1, 0 } : (vector3_t){ 0, 0, 1 };
    Matrix4_lookAt(&view, &eye, &dir, &up);
    Matrix4_ortho(&proj, -radius, radius, -radius, radius, -radius, 2 * in->reach + radius);
    Matrix4_multiply(&proj, &view, out);
    return true;
}

#endif
