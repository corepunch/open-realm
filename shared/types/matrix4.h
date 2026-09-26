#ifndef matrix4_h
#define matrix4_h

#include "vector3.h"
#include "vector4.h"
#include "quaternion.h"

struct matrix4 {
    union {
        float v[16];
        vector4_t column[4];
        float m[4][4];
    };
};

typedef struct matrix4 matrix4_t;



void Matrix4_identity(matrix4_t *m);
void Matrix4_translate(matrix4_t *m, vector3_t const *v);
void Matrix4_rotate(matrix4_t *m, vector3_t const *v, ROTATIONORDER order);
void Matrix4_scale(matrix4_t *m, vector3_t const *v);
void Matrix4_multiply(matrix4_t const *m1, matrix4_t const *m2, matrix4_t *out);
void Matrix4_ortho(matrix4_t *m, float left, float right, float bottom, float top, float znear, float zfar);
void Matrix4_perspective(matrix4_t *m, float vertical_fov, float aspect, float znear, float zfar);
void Matrix4_lookAt(matrix4_t *m, vector3_t const *eye, vector3_t const *direction, vector3_t const *up);
void Matrix4_inverse(matrix4_t const *m, matrix4_t *out);
void Matrix4_transpose(matrix4_t const *m, matrix4_t *out);
void Matrix4_rotate4(matrix4_t *m, vector4_t const *quat);
void Matrix4_from_rotation_origin(matrix4_t *out, quaternion_t const *rotation, vector3_t const *origin);
void Matrix4_from_rotation_translation_scale_origin(matrix4_t *out, quaternion_t const *q, vector3_t const *v, vector3_t const *s, vector3_t const *o);
void Matrix4_from_translation(matrix4_t *out, vector3_t const *v);
void Matrix4_rotateQuat(matrix4_t *m, quaternion_t const *quat);
vector3_t Matrix4_multiply_vector3(matrix4_t const *m1, vector3_t const *v);

#endif /* matrix4_h */
