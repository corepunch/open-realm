#include "../cmath3.h"

void Frustum_Calculate(mat4_t const *matrix, frustum3_t *output) {
    output->right.normal.x = matrix->v[ 3] - matrix->v[ 0];
    output->right.normal.y = matrix->v[ 7] - matrix->v[ 4];
    output->right.normal.z = matrix->v[11] - matrix->v[ 8];
    output->right.distance = matrix->v[15] - matrix->v[12];

    Plane3_Normalize(&output->right);

    output->left.normal.x = matrix->v[ 3] + matrix->v[ 0];
    output->left.normal.y = matrix->v[ 7] + matrix->v[ 4];
    output->left.normal.z = matrix->v[11] + matrix->v[ 8];
    output->left.distance = matrix->v[15] + matrix->v[12];

    Plane3_Normalize(&output->left);

    output->bottom.normal.x = matrix->v[ 3] + matrix->v[ 1];
    output->bottom.normal.y = matrix->v[ 7] + matrix->v[ 5];
    output->bottom.normal.z = matrix->v[11] + matrix->v[ 9];
    output->bottom.distance = matrix->v[15] + matrix->v[13];

    Plane3_Normalize(&output->bottom);

    output->top.normal.x = matrix->v[ 3] - matrix->v[ 1];
    output->top.normal.y = matrix->v[ 7] - matrix->v[ 5];
    output->top.normal.z = matrix->v[11] - matrix->v[ 9];
    output->top.distance = matrix->v[15] - matrix->v[13];

    Plane3_Normalize(&output->top);

    output->back.normal.x = matrix->v[ 3] - matrix->v[ 2];
    output->back.normal.y = matrix->v[ 7] - matrix->v[ 6];
    output->back.normal.z = matrix->v[11] - matrix->v[10];
    output->back.distance = matrix->v[15] - matrix->v[14];

    Plane3_Normalize(&output->back);

    output->front.normal.x = matrix->v[ 3] + matrix->v[ 2];
    output->front.normal.y = matrix->v[ 7] + matrix->v[ 6];
    output->front.normal.z = matrix->v[11] + matrix->v[10];
    output->front.distance = matrix->v[15] + matrix->v[14];

    Plane3_Normalize(&output->front);
}

int Frustum_ContainsSphere(frustum3_t const *frustum, sphere3_t const *sphere) {
    for (unsigned i = 0; i < FRUSTUM_NUM_PLANES; i++) {
        if (Plane3_MultiplyVector3(frustum->planes+i, &sphere->center) <= -sphere->radius) {
            return 0;
        }
    }
    return 1;
}

int Frustum_ContainsPoint(frustum3_t const *frustum, vec3_t const *point) {
    return Frustum_ContainsSphere(frustum, &(sphere3_t){.center=*point,.radius=0});
}

/* Retain the exact corner test for projective transforms and borderline
 * floating-point cases; the fast support-point test below handles affine
 * model matrices without transforming eight corners for every plane. */
static int Frustum_ContainsBoxCorners(frustum3_t const *frustum, box3_t const *box, mat4_t const *matrix) {
    vec3_t const points[] = {
        Matrix4_multiply_vector3(matrix, &(vec3_t) { box->min.x, box->min.y, box->min.z }),
        Matrix4_multiply_vector3(matrix, &(vec3_t) { box->max.x, box->min.y, box->min.z }),
        Matrix4_multiply_vector3(matrix, &(vec3_t) { box->min.x, box->max.y, box->min.z }),
        Matrix4_multiply_vector3(matrix, &(vec3_t) { box->max.x, box->max.y, box->min.z }),
        Matrix4_multiply_vector3(matrix, &(vec3_t) { box->min.x, box->min.y, box->max.z }),
        Matrix4_multiply_vector3(matrix, &(vec3_t) { box->max.x, box->min.y, box->max.z }),
        Matrix4_multiply_vector3(matrix, &(vec3_t) { box->min.x, box->max.y, box->max.z }),
        Matrix4_multiply_vector3(matrix, &(vec3_t) { box->max.x, box->max.y, box->max.z }),
    };
    for (unsigned i = 0; i < FRUSTUM_NUM_PLANES; i++) {
        for (unsigned j = 0; j < sizeof(points)/sizeof(*points); j++) {
            if (Plane3_MultiplyVector3(frustum->planes+i, points+j) > 0) {
                goto next_cycle;
            }
        }
        return 0;
    next_cycle:;
    }
    return 1;
}

int Frustum_ContainsBox(frustum3_t const *frustum, box3_t const *box, mat4_t const *matrix) {
    /* The homogeneous divide is not constant for perspective matrices.
     * Preserve their original eight-corner path (e.g. model-camera views). */
    if (matrix->v[3] != 0.0f || matrix->v[7] != 0.0f ||
        matrix->v[11] != 0.0f || matrix->v[15] != 1.0f)
        return Frustum_ContainsBoxCorners(frustum, box, matrix);

    for (unsigned i = 0; i < FRUSTUM_NUM_PLANES; i++) {
        plane3_t const *plane = &frustum->planes[i];
        /* Maximise this plane's signed distance over the affine box.
         * The affine image of a box is a parallelepiped, whose support
         * point in direction n picks one endpoint on each local axis. */
        float const dx = plane->a * matrix->v[0] + plane->b * matrix->v[1] + plane->c * matrix->v[2];
        float const dy = plane->a * matrix->v[4] + plane->b * matrix->v[5] + plane->c * matrix->v[6];
        float const dz = plane->a * matrix->v[8] + plane->b * matrix->v[9] + plane->c * matrix->v[10];
        vec3_t const corner = {
            dx > 0.0f ? box->max.x : box->min.x,
            dy > 0.0f ? box->max.y : box->min.y,
            dz > 0.0f ? box->max.z : box->min.z,
        };
        vec3_t const world = {
            matrix->v[0] * corner.x + matrix->v[4] * corner.y + matrix->v[8] * corner.z + matrix->v[12],
            matrix->v[1] * corner.x + matrix->v[5] * corner.y + matrix->v[9] * corner.z + matrix->v[13],
            matrix->v[2] * corner.x + matrix->v[6] * corner.y + matrix->v[10] * corner.z + matrix->v[14],
        };
        float const distance = Plane3_MultiplyVector3(plane, &world);
        /* Comparing exactly at a plane can change the strict > 0
         * decision through rounding or support-axis cancellation.
         * Let the original algorithm adjudicate only that narrow band. */
        float const error = 16.0f * 1.19209290e-7f *
            (fabsf(plane->a * world.x) + fabsf(plane->b * world.y) +
             fabsf(plane->c * world.z) + fabsf(plane->d) + 1.0f);
        if (distance > error)
            continue;
        if (distance < -error)
            return 0;
        return Frustum_ContainsBoxCorners(frustum, box, matrix);
    }
    return 1;
}

int Frustum_ContainsAABox(frustum3_t const *frustum, box3_t const *box) {
    vec3_t const points[] = {
        { box->min.x, box->min.y, box->min.z },
        { box->max.x, box->min.y, box->min.z },
        { box->min.x, box->max.y, box->min.z },
        { box->max.x, box->max.y, box->min.z },
        { box->min.x, box->min.y, box->max.z },
        { box->max.x, box->min.y, box->max.z },
        { box->min.x, box->max.y, box->max.z },
        { box->max.x, box->max.y, box->max.z },
    };
    for (unsigned i = 0; i < FRUSTUM_NUM_PLANES; i++) {
        for (unsigned j = 0; j < sizeof(points)/sizeof(*points); j++) {
            if (Plane3_MultiplyVector3(frustum->planes+i, points+j) > 0) {
                goto next_cycle;
            }
        }
        return 0;
    next_cycle:;
    }
    return 1;
}
