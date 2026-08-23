#ifndef vector3_h
#define vector3_h

struct vector3 { float x, y, z; };

typedef struct vector3 VECTOR3;
typedef struct vector3 *LPVECTOR3;
typedef struct vector3 const *LPCVECTOR3;

float Vector3_dot(LPCVECTOR3 a, LPCVECTOR3 b);
float Vector3_lengthsq(LPCVECTOR3 vec);
float Vector3_len(LPCVECTOR3 vec);
float Vector3_distance(LPCVECTOR3 a, LPCVECTOR3 b);
VECTOR3 Vector3_bezier(LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c, LPCVECTOR3 d, float t);
VECTOR3 Vector3_hermite(LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c, LPCVECTOR3 d, float t);
VECTOR3 Vector3_lerp(LPCVECTOR3 a, LPCVECTOR3 b, float t);
VECTOR3 Vector3_cross(LPCVECTOR3 a, LPCVECTOR3 b);
VECTOR3 Vector3_sub(LPCVECTOR3 a, LPCVECTOR3 b);
VECTOR3 Vector3_add(LPCVECTOR3 a, LPCVECTOR3 b);
VECTOR3 Vector3_mad(LPCVECTOR3 v, float s, LPCVECTOR3 b);
VECTOR3 Vector3_mul(LPCVECTOR3 a, LPCVECTOR3 b);
VECTOR3 Vector3_scale(LPCVECTOR3 v, float s);
VECTOR3 Vector3_rotateAroundAxis(LPCVECTOR3 v, LPCVECTOR3 axis, float radians);
void Vector3_normalize(LPVECTOR3 v);
void Vector3_set(LPVECTOR3 v, float x, float y, float z);
void Vector3_clear(LPVECTOR3 v);
VECTOR3 Vector3_unm(VECTOR3 const* v);
VECTOR3 Vector3_clamp01(LPCVECTOR3 v);

#endif
