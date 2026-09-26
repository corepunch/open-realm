#ifndef vector2_h
#define vector2_h

struct vector2 { float x, y; };

typedef struct vector2 vector2_t;



void Vector2_set(vector2_t * v, float x, float y);
vector2_t Vector2_scale(vector2_t const * v, float s);
vector2_t Vector2_add(vector2_t const * a, vector2_t const * b);
vector2_t Vector2_sub(vector2_t const * a, vector2_t const * b);
vector2_t Vector2_lerp(vector2_t const * a, vector2_t const * b, float t);
vector2_t Vector2_mad(vector2_t const * v, float s, vector2_t const * b);
vector2_t Vector2_unm(vector2_t const * v);
float Vector2_distance(vector2_t const * a, vector2_t const * b);
float Vector2_dot(vector2_t const * a, vector2_t const * b);
float Vector2_lengthsq(vector2_t const * vec);
float Vector2_len(vector2_t const * vec);
void Vector2_normalize(vector2_t * v);

#endif
