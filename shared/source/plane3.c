#include "../cmath3.h"

void Plane3_Normalize(plane3_t *plane) {
    // Here we calculate the magnitude of the normal to the plane (point A B C)
    // Remember that (A, B, C) is that same thing as the normal's (X, Y, Z).
    // To calculate magnitude you use the equation:  magnitude = sqrt( x^2 + y^2 + z^2)
    float magnitude = (float)sqrt( plane->normal.x * plane->normal.x +
        plane->normal.y * plane->normal.y +
        plane->normal.z * plane->normal.z );

    // Then we divide the plane's values by it's magnitude.
    // This makes it easier to work with.
    plane->normal.x /= magnitude;
    plane->normal.y /= magnitude;
    plane->normal.z /= magnitude;
    plane->distance /= magnitude;
}

float Plane3_MultiplyVector3(plane3_t const *p, vector3_t const *v) {
    return p->a * v->x + p->b * v->y + p->c * v->z + p->d;
}
