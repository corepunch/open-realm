#ifndef BZ_SERVER_ROUTING_H
#define BZ_SERVER_ROUTING_H

#define BZ_ROUTE_SLIDE_STEP (15.0f * (float)M_PI / 180.0f) // radians; WC3 deflection increment; used by local steering
#define BZ_ROUTE_SLIDE_RINGS 6 // steps/side; WC3 searches through 90 degrees; bounds local steering

#define BZ_PATH_WORK_BUDGET 32768 // queue pops/tick; WC3 default completes a 256x256 open field in two ticks

typedef struct {
    vector2_t waypoint, target;
    float radius;
    bool valid;
} routePath_t;



typedef struct {
    edict_t * ent;
    float angle, dist;
    int rings;
    bool (*valid)(edict_t * ent, vector2_t const * point);
} routeSlide_t;



/* A path texture's authored cells can be remapped by the owning game before
 * they are stamped into the shared path map.  The router owns the generic
 * quarter-turn geometry; games own the policy that selects the turn. */
typedef struct {
    int width, height, turn;
} pathTexTransform_t;

typedef struct {
    edict_t const * ent;
    pathTex_t const *pathtex;
} pathTexTransformParams_t;

float CM_SlideRoute(routeSlide_t const * slide);
bool CM_AccelerateRoute(routePath_t * path, pathAccelParams_t const *params, vector2_t * dir);
pathTexTransform_t CM_GetPathTexTransform(edict_t const * ent);
#endif
