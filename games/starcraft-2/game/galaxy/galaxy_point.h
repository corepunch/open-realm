/* galaxy_point.h — point, region, and visibility natives */

#define MAX_GALAXY_POINTS 4096
typedef struct { float x, y, height, facing; } sc2GPoint_t;
static sc2GPoint_t sc2_gpoints[MAX_GALAXY_POINTS];
static int32_t sc2_gpoint_n = 1;  /* 1-based; 0 = null handle */

static int32_t sc2_point_new(jass_t *j, sc2GPoint_t p) {
    if (sc2_gpoint_n == MAX_GALAXY_POINTS) { jass_rterror(j,"Galaxy point table full"); return 0; }
    int32_t h = sc2_gpoint_n++; sc2_gpoints[h] = p; return h;
}
static sc2GPoint_t *sc2_point(jass_t *j, int arg) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j,arg,"point");
    if (h <= 0 || h >= sc2_gpoint_n) { jass_rterror(j,"Invalid Galaxy point"); return NULL; }
    return &sc2_gpoints[h];
}
static uint32_t sc2_point_result(jass_t *j, sc2GPoint_t p) {
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)sc2_point_new(j,p),"point");
}
static bool sc2_unit_location_handle(int32_t h, sc2GPoint_t *p);

/* PointFromId: look up map point object by ID, return typed handle. */
static uint32_t sc2_PointFromId(jass_t *j) {
    uint32_t map_id = (uint32_t)jass_checkinteger(j, 1);
    float x = 0.0f, y = 0.0f;
    if (sc2_galaxy_get_point_by_id && sc2_gpoint_n < MAX_GALAXY_POINTS &&
        sc2_galaxy_get_point_by_id(map_id, &x, &y)) {
        int32_t h = sc2_gpoint_n++;
        sc2_gpoints[h] = (sc2GPoint_t){ x, y };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "point");
    }
    if (sc2_gpoint_n >= MAX_GALAXY_POINTS)
        fprintf(stderr, "PointFromId: table full (%d entries) — id=%u lost\n",
                MAX_GALAXY_POINTS, map_id);
    return jass_pushnullhandle(j, "point");
}

/* Point(x, y): create a point from explicit coordinates. */
static uint32_t sc2_Point(jass_t *j) {
    float x = jass_checknumber(j, 1), y = jass_checknumber(j, 2);
    if (sc2_gpoint_n < MAX_GALAXY_POINTS) {
        int32_t h = sc2_gpoint_n++;
        sc2_gpoints[h] = (sc2GPoint_t){ x, y };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "point");
    }
    return jass_pushnullhandle(j, "point");
}

static float sc2_point_x(jass_t *j, int idx) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, idx, "point");
    return (h > 0 && h < sc2_gpoint_n) ? sc2_gpoints[h].x : 0.0f;
}
static float sc2_point_y(jass_t *j, int idx) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, idx, "point");
    return (h > 0 && h < sc2_gpoint_n) ? sc2_gpoints[h].y : 0.0f;
}

static uint32_t sc2_PointGetX(jass_t *j) { return jass_pushnumber(j, sc2_point_x(j, 1)); }
static uint32_t sc2_PointGetY(jass_t *j) { return jass_pushnumber(j, sc2_point_y(j, 1)); }
static uint32_t sc2_PointGetHeight(jass_t *j) { return jass_pushnumber(j,sc2_point(j,1)->height); }
static uint32_t sc2_PointGetFacing(jass_t *j) { return jass_pushnumber(j,sc2_point(j,1)->facing); }

/* PointWithOffset: returns a new point offset from the base. */
static uint32_t sc2_PointWithOffset(jass_t *j) {
    float bx = sc2_point_x(j, 1), by = sc2_point_y(j, 1);
    float dx = jass_checknumber(j, 2), dy = jass_checknumber(j, 3);
    if (sc2_gpoint_n < MAX_GALAXY_POINTS) {
        int32_t h = sc2_gpoint_n++;
        sc2_gpoints[h] = (sc2GPoint_t){ bx + dx, by + dy };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "point");
    }
    return jass_pushnullhandle(j, "point");
}

/* PointWithOffsetPolar: returns a new point offset by (dist, angle_deg). */
static uint32_t sc2_PointWithOffsetPolar(jass_t *j) {
    float bx = sc2_point_x(j, 1), by = sc2_point_y(j, 1);
    float dist = jass_checknumber(j, 2);
    float ang  = jass_checknumber(j, 3) * 3.14159265f / 180.0f; /* degrees → radians */
    if (sc2_gpoint_n < MAX_GALAXY_POINTS) {
        int32_t h = sc2_gpoint_n++;
        sc2_gpoints[h] = (sc2GPoint_t){ bx + dist * cosf(ang), by + dist * sinf(ang) };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "point");
    }
    return jass_pushnullhandle(j, "point");
}

static uint32_t sc2_PointReflect(jass_t *j) { sc2GPoint_t a = *sc2_point(j,1), b = *sc2_point(j,2);
    float angle = jass_checknumber(j,3) * (float)M_PI / 180.0f;
    float dx = a.x-b.x, dy = a.y-b.y, c = cosf(2*angle), s = sinf(2*angle);
    a.x = b.x + dx*c + dy*s; a.y = b.y + dx*s - dy*c;
    return sc2_point_result(j,a); }
static uint32_t sc2_PointPathingCliffLevel(jass_t *j){ return jass_pushinteger(j, 0); }
static uint32_t sc2_PointSetFacing(jass_t *j) { sc2_point(j,1)->facing = jass_checknumber(j,2); return 0; }

static uint32_t sc2_AngleBetweenPoints(jass_t *j) {
    float ax = sc2_point_x(j, 1), ay = sc2_point_y(j, 1);
    float bx = sc2_point_x(j, 2), by = sc2_point_y(j, 2);
    return jass_pushnumber(j, atan2f(by - ay, bx - ax) * 180.0f / 3.14159265f);
}

static uint32_t sc2_DistanceBetweenPoints(jass_t *j) {
    float ax = sc2_point_x(j, 1), ay = sc2_point_y(j, 1);
    float bx = sc2_point_x(j, 2), by = sc2_point_y(j, 2);
    float dx = bx - ax, dy = by - ay;
    return jass_pushnumber(j, sqrtf(dx*dx + dy*dy));
}

static uint32_t sc2_PointSetHeight(jass_t *j) { sc2_point(j,1)->height = jass_checknumber(j,2); return 0; }
static uint32_t sc2_PointSet(jass_t *j) { *sc2_point(j,1) = *sc2_point(j,2); return 0; }
static uint32_t sc2_PointsInRange(jass_t *j) {
    sc2GPoint_t a = *sc2_point(j,1), b = *sc2_point(j,2); float r = jass_checknumber(j,3);
    return jass_pushboolean(j,r >= 0 && (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y) <= r*r);
}
#include "galaxy_region.h"

static uint32_t sc2_VisRevealArea(jass_t *j)         { (void)j; return jass_pushnull(j); }
static uint32_t sc2_VisExploreArea(jass_t *j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_VisRevealerCreate(jass_t *j)     { return jass_pushnullhandle(j, "revealer"); }
static uint32_t sc2_VisRevealerLastCreated(jass_t *j){ return jass_pushnullhandle(j, "revealer"); }
static uint32_t sc2_VisRevealerDestroy(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_VisEnable(jass_t *j)             { (void)j; return jass_pushnull(j); }
