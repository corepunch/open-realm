#ifndef frustum3_h
#define frustum3_h

#include "plane3.h"
#include "sphere3.h"
#include "box3.h"

enum {
    FRUSTUM_LEFT,
    FRUSTUM_RIGHT,
    FRUSTUM_BOTTOM,
    FRUSTUM_TOP,
    FRUSTUM_BACK,
    FRUSTUM_FRONT,
    FRUSTUM_NUM_PLANES
};

struct frustum3 {
    union {
        struct { plane3_t left, right, bottom, top, front, back; };
        plane3_t planes[FRUSTUM_NUM_PLANES];
    };
};

typedef struct frustum3 frustum3_t;



void Frustum_Calculate(mat4_t const *matrix, frustum3_t *output);
int Frustum_ContainsPoint(frustum3_t const *frustum, vec3_t const *point);
int Frustum_ContainsSphere(frustum3_t const *frustum, sphere3_t const *sphere);
int Frustum_ContainsBox(frustum3_t const *frustum, box3_t const *box, mat4_t const *matrix);
int Frustum_ContainsAABox(frustum3_t const *frustum, box3_t const *box);

#endif
