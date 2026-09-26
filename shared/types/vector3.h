#ifndef vector3_h
#define vector3_h

struct vector3 { float x, y, z; };

typedef struct vector3 vector3_t;



float Vector3_dot(vector3_t const *a, vector3_t const *b);
float Vector3_lengthsq(vector3_t const *vec);
float Vector3_len(vector3_t const *vec);
float Vector3_distance(vector3_t const *a, vector3_t const *b);
vector3_t Vector3_bezier(vector3_t const *a, vector3_t const *b, vector3_t const *c, vector3_t const *d, float t);
vector3_t Vector3_hermite(vector3_t const *a, vector3_t const *b, vector3_t const *c, vector3_t const *d, float t);
vector3_t Vector3_lerp(vector3_t const *a, vector3_t const *b, float t);
vector3_t Vector3_cross(vector3_t const *a, vector3_t const *b);
vector3_t Vector3_sub(vector3_t const *a, vector3_t const *b);
vector3_t Vector3_add(vector3_t const *a, vector3_t const *b);
vector3_t Vector3_mad(vector3_t const *v, float s, vector3_t const *b);
vector3_t Vector3_mul(vector3_t const *a, vector3_t const *b);
vector3_t Vector3_scale(vector3_t const *v, float s);
vector3_t Vector3_rotateAroundAxis(vector3_t const *v, vector3_t const *axis, float radians);
void Vector3_normalize(vector3_t *v);
void Vector3_set(vector3_t *v, float x, float y, float z);
void Vector3_clear(vector3_t *v);
vector3_t Vector3_unm(vector3_t const* v);
vector3_t Vector3_clamp01(vector3_t const *v);

#endif
