#ifndef line3_h
#define line3_h

#include "vector3.h"
#include "sphere3.h"
#include "plane3.h"
#include "box3.h"
#include "triangle3.h"

struct line3 {
    vec3_t a;
    vec3_t b;
};

typedef struct line3 line3_t;



int Line3_intersect_sphere3(line3_t const *line, sphere3_t const *sphere, vec3_t *output);
int Line3_intersect_plane3(line3_t const *line, plane3_t const *plane, vec3_t *output);
int Line3_intersect_triangle(line3_t const *line, triangle3_t const *triangle, vec3_t *output);
int Line3_intersect_box3(line3_t const *line, box3_t const *box, vec3_t *output);

#endif
