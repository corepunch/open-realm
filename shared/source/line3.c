#include "../cmath3.h"

int Line3_intersect_sphere3(line3_t const *line, sphere3_t const *sphere, vec3_t *output) {
    float cx = sphere->center.x;
    float cy = sphere->center.y;
    float cz = sphere->center.z;
    float px = line->a.x;
    float py = line->a.y;
    float pz = line->a.z;
    float vx = line->b.x - px;
    float vy = line->b.y - py;
    float vz = line->b.z - pz;
    float A = vx * vx + vy * vy + vz * vz;
    float B = 2.0 * (px * vx + py * vy + pz * vz - vx * cx - vy * cy - vz * cz);
    float C = px * px - 2 * px * cx + cx * cx + py * py - 2 * py * cy + cy * cy +
               pz * pz - 2 * pz * cz + cz * cz - sphere->radius * sphere->radius;
    float D = B * B - 4 * A * C;

    if ( D < 0 ) {
        return 0;
    } else if (!output) {
        return 1;
    }

    float t1 = ( -B - sqrtf ( D ) ) / ( 2.0 * A );

    vec3_t solution1 = {
        line->a.x * ( 1 - t1 ) + t1 * line->b.x,
        line->a.y * ( 1 - t1 ) + t1 * line->b.y,
        line->a.z * ( 1 - t1 ) + t1 * line->b.z
    };

    if ( D == 0 ) {
        *output = solution1;
        return 1;
    }

    float t2 = ( -B + sqrtf( D ) ) / ( 2.0 * A );
    vec3_t solution2 = {
        line->a.x * ( 1 - t2 ) + t2 * line->b.x,
        line->a.y * ( 1 - t2 ) + t2 * line->b.y,
        line->a.z * ( 1 - t2 ) + t2 * line->b.z
    };

    if ( fabs( t1 - 0.5 ) < fabs( t2 - 0.5 ) ) {
        *output = solution1;
        return 1;
    }

    *output = solution2;
    return 1;
}

int Line3_intersect_plane3(line3_t const *line, plane3_t const *plane, vec3_t *output) {
    vec3_t direction = Vector3_sub(&line->b, &line->a);
    float dotProduct = Vector3_dot(&direction, &plane->normal);
    // Check if the line and plane are parallel
    if (fabs(dotProduct) < EPSILON) {
        return 0;
    }
    if (output) {
        float t = -Plane3_MultiplyVector3(plane, &line->a) / dotProduct;
        *output = Vector3_mad(&line->a, t, &direction);
    }
    return 1;
}

static inline float DotNormal(vec3_t const *normal, vec3_t const *a, vec3_t const *b, vec3_t const *c) {
    vec3_t const rcross = Triangle_normal(&(const triangle3_t) { *a, *b, *c });
    return Vector3_dot(normal, &rcross);
}

int Line3_intersect_triangle(line3_t const *line, triangle3_t const *triangle, vec3_t *output) {
    vec3_t const normal = Triangle_normal(triangle);
    vec3_t const diff1 = Vector3_sub(&line->a, &triangle->a);
    vec3_t const diff2 = Vector3_sub(&line->b, &triangle->a);
    float const r1 = Vector3_dot(&normal, &diff1);
    float const r2 = Vector3_dot(&normal, &diff2);
    if ((r1 > 0) == (r2 > 0))
        return 0;
    vec3_t const distance = Vector3_sub(&line->a, &line->b);
    vec3_t const pc = Vector3_mad(&line->a, r1 / (r2 - r1), &distance);
    if (DotNormal(&normal, &triangle->a, &triangle->b, &pc) < 0)
        return 0;
    if (DotNormal(&normal, &triangle->b, &triangle->c, &pc) < 0)
        return 0;
    if (DotNormal(&normal, &triangle->c, &triangle->a, &pc) < 0)
        return 0;
    if (output)
        *output = pc;
    return 1;
}
//
//bool intersect_triangle(
//    in Ray R, in vec3 A, in vec3 B, in vec3 C, out float t,
//    out float u, out float v, out vec3 N
//) {
//   vec3 E1 = B-A;
//   vec3 E2 = C-A;
//         N = cross(E1,E2);
//   float det = -dot(R.Dir, N);
//   float invdet = 1.0/det;
//   vec3 AO  = R.Origin - A;
//   vec3 DAO = cross(AO, R.Dir);
//   u =  dot(E2,DAO) * invdet;
//   v = -dot(E1,DAO) * invdet;
//   t =  dot(AO,N)  * invdet;
//   return (det >= 1e-6 && t >= 0.0 && u >= 0.0 && v >= 0.0 && (u+v) <= 1.0);
//}

int Line3_intersect_box3(line3_t const *line, box3_t const *box, vec3_t *output) {
    float st, et, fst = 0, fet = 1;
    float const *bmin = &box->min.x;
    float const *bmax = &box->max.x;
    float const *si = &line->a.x;
    float const *ei = &line->b.x;
    for (int i = 0; i < 3; i++) {
        if (*si < *ei) {
            if (*si > *bmax || *ei < *bmin)
                return 0;
            float di = *ei - *si;
            st = (*si < *bmin)? (*bmin - *si) / di: 0;
            et = (*ei > *bmax)? (*bmax - *si) / di: 1;
        }
        else {
            if (*ei > *bmax || *si < *bmin)
                return 0;
            float di = *ei - *si;
            st = (*si > *bmax)? (*bmax - *si) / di: 0;
            et = (*ei < *bmin)? (*bmin - *si) / di: 1;
        }

        if (st > fst) fst = st;
        if (et < fet) fet = et;
        if (fet < fst)
            return 0;
        bmin++; bmax++;
        si++; ei++;
    }

    if (output) {
        *output = Vector3_lerp(&line->a, &line->b, fst);
    }
    return 1;
}
