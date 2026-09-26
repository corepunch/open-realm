#ifndef vector2_h
#define vector2_h

struct vector2 { float x, y; };

typedef struct vector2 vec2_t;



void Vector2_set(vec2_t *v, float x, float y);
vec2_t Vector2_scale(vec2_t const *v, float s);
vec2_t Vector2_add(vec2_t const *a, vec2_t const *b);
vec2_t Vector2_sub(vec2_t const *a, vec2_t const *b);
vec2_t Vector2_lerp(vec2_t const *a, vec2_t const *b, float t);
vec2_t Vector2_mad(vec2_t const *v, float s, vec2_t const *b);
vec2_t Vector2_unm(vec2_t const *v);
float Vector2_distance(vec2_t const *a, vec2_t const *b);
float Vector2_dot(vec2_t const *a, vec2_t const *b);
float Vector2_lengthsq(vec2_t const *vec);
float Vector2_len(vec2_t const *vec);
void Vector2_normalize(vec2_t *v);

#endif
