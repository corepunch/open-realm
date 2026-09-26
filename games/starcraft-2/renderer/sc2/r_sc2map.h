#ifndef R_SC2MAP_H
#define R_SC2MAP_H

#include "renderer/r_game.h"
#include "renderer/r_cliff.h"
#include "renderer/r_camera_height.h"
#include "games/starcraft-2/common/sc2_map.h"

typedef struct SC2ROADTRI {
    VERTEX verts[3];
    float depth;
} SC2ROADTRI;
typedef SC2ROADTRI *LPSC2ROADTRI;
typedef SC2ROADTRI const *LPCSC2ROADTRI;

void      R_SC2ShutdownShaders(void);
void      R_SC2RegisterMap(cstring_t mapFileName);
void      R_SC2DrawWorld(void);
bool      R_SC2TraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 output);
float     R_SC2GetHeightAtPoint(float x, float y);
float     R_SC2GetCameraHeightAtPoint(float x, float y);
VECTOR2   R_SC2WorldSize(void);

/* HRDT deforms a unit cube between endpoint offsets, with its top on the authored surface. */
static inline void r_sc2_hard_tile_matrix(sc2MapHardTile_t const *tile, LPMATRIX4 matrix) {
	VECTOR3 along = Vector3_sub(&tile->end, &tile->start);
	VECTOR3 side = Vector3_cross(&along, &tile->normal);
	VECTOR3 base = Vector3_scale(&tile->normal, -tile->scale.y);

	Vector3_normalize(&side); side = Vector3_scale(&side, tile->scale.x * 2.0f);
	Matrix4_identity(matrix);
	/* HRDT scaleX is road half-width; start/end define its longitudinal Y span. */
	matrix->v[0] = side.x; matrix->v[1] = side.y; matrix->v[2] = side.z;
	matrix->v[4] = along.x; matrix->v[5] = along.y; matrix->v[6] = along.z;
	matrix->v[8] = tile->normal.x * tile->scale.y; matrix->v[9] = tile->normal.y * tile->scale.y; matrix->v[10] = tile->normal.z * tile->scale.y;
	matrix->v[12] = tile->position.x + base.x; matrix->v[13] = tile->position.y + base.y; matrix->v[14] = tile->position.z + base.z;
}

/* Signed XY area gives both clipping half-planes and barycentric surface weights. */
static inline float r_sc2_road_side(VECTOR3 a, VECTOR3 b, VECTOR3 p) {
    return (b.x-a.x)*(p.y-a.y) - (b.y-a.y)*(p.x-a.x);
}

/* Intersect a ribbon triangle with one ground triangle, retaining road UVs.
   Clipping at cell diagonals is essential: vertex-only draping spans terrain folds. */
static inline uint32_t r_sc2_clip_road(LPCVERTEX road, LPCVERTEX ground, LPVERTEX out) {
    VERTEX poly[8], scratch[8];
    uint32_t count = 3, total = 0;
    float area = r_sc2_road_side(ground[0].position, ground[1].position, ground[2].position);

    if (fabsf(area) < 1e-8f) return 0;
    memcpy(poly, road, 3 * sizeof(*poly));
    FOR_LOOP(edge, 3) {
        VECTOR3 a = ground[edge].position, b = ground[(edge+1)%3].position;
        uint32_t n = 0;
        for (uint32_t i = 0; i < count; i++) {
            VERTEX prev = poly[(i+count-1)%count], cur = poly[i];
            float sign = area > 0 ? 1 : -1;
            float d0 = sign*r_sc2_road_side(a, b, prev.position), d1 = sign*r_sc2_road_side(a, b, cur.position);
            if ((d0 < 0) != (d1 < 0)) {
                float t = d0 / (d0-d1);
                VERTEX v = prev;
                v.position = Vector3_lerp(&prev.position, &cur.position, t);
                v.texcoord = Vector2_lerp(&prev.texcoord, &cur.texcoord, t);
                scratch[n++] = v;
            }
            if (d1 >= 0) scratch[n++] = cur;
        }
        count = n; memcpy(poly, scratch, n * sizeof(*poly));
    }
    FOR_LOOP(i, count) {
        VECTOR3 p = poly[i].position;
        float u = r_sc2_road_side(ground[1].position, ground[2].position, p) / area;
        float v = r_sc2_road_side(ground[2].position, ground[0].position, p) / area;
        float w = 1-u-v;
        /* Authored spline Z must not lift the road above ground rings. */
        poly[i].position.z = u*ground[0].position.z + v*ground[1].position.z + w*ground[2].position.z;
        poly[i].normal = (VECTOR3){
            u*ground[0].normal.x + v*ground[1].normal.x + w*ground[2].normal.x,
            u*ground[0].normal.y + v*ground[1].normal.y + w*ground[2].normal.y,
            u*ground[0].normal.z + v*ground[1].normal.z + w*ground[2].normal.z };
    }
    for (uint32_t i = 1; i+1 < count; i++) {
        if (fabsf(r_sc2_road_side(poly[0].position, poly[i].position, poly[i+1].position)) < 1e-8f) continue;
        if (out) { out[total] = poly[0]; out[total+1] = poly[i]; out[total+2] = poly[i+1]; }
        total += 3;
    }
    return total;
}

/* Distance below the authored ribbon plane; HRDT depth bounds projection onto cliff tops. */
static inline float r_sc2_road_depth(LPCVERTEX road, VECTOR3 p) {
    float area = r_sc2_road_side(road[0].position, road[1].position, road[2].position);
    float u = r_sc2_road_side(road[1].position, road[2].position, p)/area;
    float v = r_sc2_road_side(road[2].position, road[0].position, p)/area;
    return u*road[0].position.z + v*road[1].position.z + (1-u-v)*road[2].position.z - p.z;
}

/* Cliff meshes replace grid cells at bridge ends. Clip to the authored depth envelope so roads
   reach their actual surface without being projected all the way down the canyon walls. */
static inline uint32_t r_sc2_clip_road_cliff(LPCSC2ROADTRI road, LPCVERTEX cliff, LPVERTEX out) {
    VERTEX clipped[18];
    uint32_t total = 0;
    if (fabsf(r_sc2_road_side(road->verts[0].position, road->verts[1].position, road->verts[2].position)) < 1e-8f) return 0;
    uint32_t n = r_sc2_clip_road(road->verts, cliff, clipped);
    for (uint32_t i = 0; i < n; i += 3) {
        VERTEX poly[6], scratch[6];
        uint32_t count = 3;
        memcpy(poly, clipped+i, 3*sizeof(*poly));
        FOR_LOOP(plane, 2) {
            uint32_t used = 0;
            float sign = plane ? -1 : 1;
            FOR_LOOP(j, count) {
                VERTEX prev = poly[(j+count-1)%count], cur = poly[j];
                float d0 = road->depth - sign*r_sc2_road_depth(road->verts, prev.position);
                float d1 = road->depth - sign*r_sc2_road_depth(road->verts, cur.position);
                if ((d0 < 0) != (d1 < 0)) {
                    float t = d0/(d0-d1);
                    VERTEX v = prev;
                    v.position = Vector3_lerp(&prev.position, &cur.position, t);
                    v.texcoord = Vector2_lerp(&prev.texcoord, &cur.texcoord, t);
                    v.normal = Vector3_lerp(&prev.normal, &cur.normal, t);
                    scratch[used++] = v;
                }
                if (d1 >= 0) scratch[used++] = cur;
            }
            count = used; memcpy(poly, scratch, count*sizeof(*poly));
        }
        for (uint32_t j = 1; j+1 < count; j++) {
            if (fabsf(r_sc2_road_side(poly[0].position, poly[j].position, poly[j+1].position)) < 1e-8f) continue;
            if (out) { out[total] = poly[0]; out[total+1] = poly[j]; out[total+2] = poly[j+1]; }
            total += 3;
        }
    }
    return total;
}

static inline VECTOR3 r_sc2_hard_tile_curve_point(sc2MapHardTile_t const *a, sc2MapHardTile_t const *b, float t) {
	VECTOR3 p1 = Vector3_add(&a->position, &a->end), p2 = Vector3_add(&b->position, &b->start);
	VECTOR3 ab = Vector3_lerp(&a->position, &p1, t), bc = Vector3_lerp(&p1, &p2, t), cd = Vector3_lerp(&p2, &b->position, t);
	VECTOR3 abc = Vector3_lerp(&ab, &bc, t), bcd = Vector3_lerp(&bc, &cd, t);
	return Vector3_lerp(&abc, &bcd, t);
}

static inline VECTOR3 r_sc2_hard_tile_curve_tangent(sc2MapHardTile_t const *a, sc2MapHardTile_t const *b, float t) {
	VECTOR3 p1 = Vector3_add(&a->position, &a->end), p2 = Vector3_add(&b->position, &b->start);
	VECTOR3 d0 = Vector3_sub(&p1, &a->position), d1 = Vector3_sub(&p2, &p1), d2 = Vector3_sub(&b->position, &p2);
	float u = 1.0f - t;
	VECTOR3 tangent = Vector3_add(&(VECTOR3){d0.x * u * u, d0.y * u * u, d0.z * u * u},
		&(VECTOR3){d1.x * 2.0f * u * t + d2.x * t * t, d1.y * 2.0f * u * t + d2.y * t * t, d1.z * 2.0f * u * t + d2.z * t * t});
	return tangent;
}


#endif
