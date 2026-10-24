#ifndef matrix4_h
#define matrix4_h

#include "vector3.h"
#include "vector4.h"
#include "quaternion.h"

struct matrix4 {
    union {
        float v[16];
        vec4_t column[4];
        float m[4][4];
    };
};

typedef struct matrix4 mat4_t;



void Matrix4_identity(mat4_t *m);
void Matrix4_translate(mat4_t *m, vec3_t const *v);
void Matrix4_rotate(mat4_t *m, vec3_t const *v, ROTATIONORDER order);
void Matrix4_scale(mat4_t *m, vec3_t const *v);
void Matrix4_multiply(mat4_t const *m1, mat4_t const *m2, mat4_t *out);

/* Fast composition for *affine* matrices only (last row = 0,0,0,1).
 * MDX node TRS and billboard matrices always satisfy this invariant. Do not
 * use for projection matrices; keep Matrix4_multiply for arbitrary 4x4 input.
 * As with Matrix4_multiply, out must not overlap either operand. */
static inline void Matrix4_multiply_affine(mat4_t const *left, mat4_t const *right, mat4_t *out) {
    /* Three linear columns: no translation or homogeneous-row products. */
    for (int col = 0; col < 3; col++) {
        int const offset = 4 * col;
        for (int row = 0; row < 3; row++) {
            out->v[offset + row] = left->v[row] * right->v[offset] +
                left->v[4 + row] * right->v[offset + 1] +
                left->v[8 + row] * right->v[offset + 2];
        }
        out->v[offset + 3] = 0.0f;
    }
    /* Translation column includes the left-hand translation once. */
    for (int row = 0; row < 3; row++) {
        out->v[12 + row] = left->v[row] * right->v[12] +
            left->v[4 + row] * right->v[13] +
            left->v[8 + row] * right->v[14] + left->v[12 + row];
    }
    out->v[15] = 1.0f;
}
void Matrix4_ortho(mat4_t *m, float left, float right, float bottom, float top, float znear, float zfar);
void Matrix4_perspective(mat4_t *m, float vertical_fov, float aspect, float znear, float zfar);
void Matrix4_lookAt(mat4_t *m, vec3_t const *eye, vec3_t const *direction, vec3_t const *up);
void Matrix4_inverse(mat4_t const *m, mat4_t *out);
void Matrix4_transpose(mat4_t const *m, mat4_t *out);
void Matrix4_rotate4(mat4_t *m, vec4_t const *quat);
void Matrix4_from_rotation_origin(mat4_t *out, quaternion_t const *rotation, vec3_t const *origin);
void Matrix4_from_rotation_translation_scale_origin(mat4_t *out, quaternion_t const *q, vec3_t const *v, vec3_t const *s, vec3_t const *o);
void Matrix4_from_translation(mat4_t *out, vec3_t const *v);
void Matrix4_rotateQuat(mat4_t *m, quaternion_t const *quat);
vec3_t Matrix4_multiply_vector3(mat4_t const *m1, vec3_t const *v);

#endif /* matrix4_h */
