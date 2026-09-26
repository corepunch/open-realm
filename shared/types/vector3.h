#ifndef vector3_h
#define vector3_h

struct vector3 { float x, y, z; };

typedef struct vector3 vec3_t;



float Vector3_dot(vec3_t const *a, vec3_t const *b);
float Vector3_lengthsq(vec3_t const *vec);
float Vector3_len(vec3_t const *vec);
float Vector3_distance(vec3_t const *a, vec3_t const *b);
vec3_t Vector3_bezier(vec3_t const *a, vec3_t const *b, vec3_t const *c, vec3_t const *d, float t);
vec3_t Vector3_hermite(vec3_t const *a, vec3_t const *b, vec3_t const *c, vec3_t const *d, float t);
vec3_t Vector3_lerp(vec3_t const *a, vec3_t const *b, float t);
vec3_t Vector3_cross(vec3_t const *a, vec3_t const *b);
vec3_t Vector3_sub(vec3_t const *a, vec3_t const *b);
vec3_t Vector3_add(vec3_t const *a, vec3_t const *b);
vec3_t Vector3_mad(vec3_t const *v, float s, vec3_t const *b);
vec3_t Vector3_mul(vec3_t const *a, vec3_t const *b);
vec3_t Vector3_scale(vec3_t const *v, float s);
vec3_t Vector3_rotateAroundAxis(vec3_t const *v, vec3_t const *axis, float radians);
void Vector3_normalize(vec3_t *v);
void Vector3_set(vec3_t *v, float x, float y, float z);
void Vector3_clear(vec3_t *v);
vec3_t Vector3_unm(vec3_t const* v);
vec3_t Vector3_clamp01(vec3_t const *v);

#endif
