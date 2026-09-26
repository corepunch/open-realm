#include "../cmath3.h"

void Frustum_Calculate(matrix4_t const * matrix, frustum3_t * output) {
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

int Frustum_ContainsSphere(frustum3_t const * frustum, sphere3_t const * sphere) {
    for (unsigned i = 0; i < FRUSTUM_NUM_PLANES; i++) {
        if (Plane3_MultiplyVector3(frustum->planes+i, &sphere->center) <= -sphere->radius) {
            return 0;
        }
    }
    return 1;
}

int Frustum_ContainsPoint(frustum3_t const * frustum, vector3_t const * point) {
    return Frustum_ContainsSphere(frustum, &(sphere3_t){.center=*point,.radius=0});
}

int Frustum_ContainsBox(frustum3_t const * frustum, box3_t const * box, matrix4_t const * matrix) {
    vector3_t const points[] = {
        Matrix4_multiply_vector3(matrix, &(vector3_t) { box->min.x, box->min.y, box->min.z }),
        Matrix4_multiply_vector3(matrix, &(vector3_t) { box->max.x, box->min.y, box->min.z }),
        Matrix4_multiply_vector3(matrix, &(vector3_t) { box->min.x, box->max.y, box->min.z }),
        Matrix4_multiply_vector3(matrix, &(vector3_t) { box->max.x, box->max.y, box->min.z }),
        Matrix4_multiply_vector3(matrix, &(vector3_t) { box->min.x, box->min.y, box->max.z }),
        Matrix4_multiply_vector3(matrix, &(vector3_t) { box->max.x, box->min.y, box->max.z }),
        Matrix4_multiply_vector3(matrix, &(vector3_t) { box->min.x, box->max.y, box->max.z }),
        Matrix4_multiply_vector3(matrix, &(vector3_t) { box->max.x, box->max.y, box->max.z }),
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

int Frustum_ContainsAABox(frustum3_t const * frustum, box3_t const * box) {
    vector3_t const points[] = {
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
