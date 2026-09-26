#ifndef quaternion_h
#define quaternion_h

typedef enum {
    ROTATE_XYZ,
    ROTATE_XZY,
    ROTATE_YZX,
    ROTATE_YXZ,
    ROTATE_ZXY,
    ROTATE_ZYX
} ROTATIONORDER;

/* Radians in the canonical +X-forward, +Y-left, +Z-up frame. Positive yaw turns
 * left, pitch raises the nose, and roll rotates about forward. Order: yaw * pitch * roll. */
typedef struct { float yaw, pitch, roll; } orientation_t;

struct quaternion { float x, y, z, w; };

typedef struct quaternion quaternion_t;
typedef struct matrix4 mat4_t;



float Quaternion_dotProduct(quaternion_t const *left, quaternion_t const *right);
float Quaternion_length(quaternion_t const *param);

quaternion_t Quaternion_fromOrientation(orientation_t const *angles);
quaternion_t Quaternion_fromEuler(vec3_t const *euler, ROTATIONORDER order);
quaternion_t Quaternion_unm(quaternion_t const *param);
quaternion_t Quaternion_normalized(quaternion_t const *param);
quaternion_t Quaternion_fromMatrix(mat4_t const *mat);
quaternion_t Quaternion_slerp(quaternion_t const *p, quaternion_t const *q, float t);
quaternion_t Quaternion_sqlerp(quaternion_t const *a, quaternion_t const *b, quaternion_t const *c, quaternion_t const *d, float t);

#endif
