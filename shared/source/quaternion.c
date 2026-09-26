#include "../cmath3.h"

quaternion_t Quaternion_slerp(quaternion_t const * a, quaternion_t const * b, float t) {
    float ax = a->x, ay = a->y, az = a->z, aw = a->w;
    float bx = b->x, by = b->y, bz = b->z, bw = b->w;
    float omega, cosom, sinom, scale0, scale1;
    cosom = ax * bx + ay * by + az * bz + aw * bw;
    if (cosom < 0.0f) {
        cosom = -cosom;
        bx = -bx;
        by = -by;
        bz = -bz;
        bw = -bw;
    }
    if (1.0f - cosom > EPSILON) {
        omega = acosf(cosom);
        sinom = sinf(omega);
        scale0 = sinf((1.0f - t) * omega) / sinom;
        scale1 = sinf(t * omega) / sinom;
    } else {
        scale0 = 1.0f - t;
        scale1 = t;
    }
    return (quaternion_t) {
        .x = scale0 * ax + scale1 * bx,
        .y = scale0 * ay + scale1 * by,
        .z = scale0 * az + scale1 * bz,
        .w = scale0 * aw + scale1 * bw,
    };
}

quaternion_t Quaternion_sqlerp(quaternion_t const * a, quaternion_t const * b, quaternion_t const * c, quaternion_t const * d, float t) {
    quaternion_t temp1 = Quaternion_slerp(a, d, t);
    quaternion_t temp2 = Quaternion_slerp(b, c, t);
    return Quaternion_slerp(&temp1, &temp2, 2 * t * (1 - t));
}

float Quaternion_dotProduct(quaternion_t const * left, quaternion_t const * right) {
    return left->w * right->w + left->x * right->x + left->y * right->y + left->z * right->z;
}

float Quaternion_length(quaternion_t const * param) {
    return sqrt(Quaternion_dotProduct(param, param));
}

quaternion_t Quaternion_unm(quaternion_t const * param) {
    return Quaternion_normalized(&(quaternion_t) {
        .x = -param->x,
        .y = -param->y,
        .z = -param->z,
        .w =  param->w,
    });
}

quaternion_t Quaternion_normalized(quaternion_t const * param) {
    quaternion_t r;
    float length = Quaternion_length(param);
    if (length < EPSILON)
        return *param;
    r.x = param->x / length;
    r.y = param->y / length;
    r.z = param->z / length;
    r.w = param->w / length;
    return r;
}

quaternion_t Quaternion_fromMatrix(matrix4_t const * mat) {
    quaternion_t r;

    // Algorithm in Ken Shoemake's article in 1987 SIGGRAPH course notes
    // article "Quaternion Calculus and Fast Animation".

    float fTrace = mat->m[0][0] + mat->m[1][1] + mat->m[2][2];
    float fRoot;

    if (fTrace > 0.0) {
        // |w| > 1/2, may as well choose w > 1/2
        fRoot = sqrt(fTrace + 1.0f);  // 2w
        r.w = 0.5f * fRoot;
        fRoot = 0.5f / fRoot;  // 1/(4w)
        r.x = (mat->m[1][2] - mat->m[2][1]) * fRoot;
        r.y = (mat->m[2][0] - mat->m[0][2]) * fRoot;
        r.z = (mat->m[0][1] - mat->m[1][0]) * fRoot;
    } else {
        // |w| <= 1/2
        static int s_iNext[3] = { 1, 2, 0 };
        int i = 0;
        if ( mat->m[1][1] > mat->m[0][0] )
            i = 1;
        if ( mat->m[2][2] > mat->m[i][i] )
            i = 2;
        int j = s_iNext[i];
        int k = s_iNext[j];
        fRoot = sqrt(mat->m[i][i] - mat->m[j][j] - mat->m[k][k] + 1.0f);
        float* apkQuat[3] = { &r.x, &r.y, &r.z };
        *apkQuat[i] = 0.5f*fRoot;
        fRoot = 0.5f/fRoot;
        r.w = (mat->m[j][k] - mat->m[k][j]) * fRoot;
        *apkQuat[j] = (mat->m[i][j] + mat->m[j][i]) * fRoot;
        *apkQuat[k] = (mat->m[i][k] + mat->m[k][i]) * fRoot;
    }

    return Quaternion_normalized(&r);
}

quaternion_t Quaternion_fromEuler(vector3_t const * euler, ROTATIONORDER order) {
    matrix4_t tmp;
    Matrix4_identity(&tmp);
    Matrix4_rotate(&tmp, euler, order);
    return Quaternion_fromMatrix(&tmp);
}

quaternion_t Quaternion_fromOrientation(orientation_t const *angles) {
    float cy = cosf(angles->yaw * 0.5f), sy = sinf(angles->yaw * 0.5f);
    float cp = cosf(-angles->pitch * 0.5f), sp = sinf(-angles->pitch * 0.5f);
    float cr = cosf(angles->roll * 0.5f), sr = sinf(angles->roll * 0.5f);
    return (quaternion_t){ sr*cp*cy - cr*sp*sy, cr*sp*cy + sr*cp*sy,
        cr*cp*sy - sr*sp*cy, cr*cp*cy + sr*sp*sy };
}
