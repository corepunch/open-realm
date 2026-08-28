#ifndef R_SC2MAP_H
#define R_SC2MAP_H

#include "renderer/r_game.h"
#include "games/starcraft-2/common/sc2_map.h"

#define SC2_HARD_TILE_Z_BIAS 0.05f // world units; prevents terrain z-fighting without the old visible 0.15 lift

void      R_SC2ShutdownShaders(void);
void      R_SC2RegisterMap(LPCSTR mapFileName);
void      R_SC2DrawWorld(void);
bool      R_SC2TraceLocation(viewDef_t const *viewdef, FLOAT x, FLOAT y, LPVECTOR3 output);
FLOAT     R_SC2GetHeightAtPoint(FLOAT x, FLOAT y);
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

static inline FLOAT r_sc2_hard_tile_surface_z(FLOAT authored_z, FLOAT terrain_z) {
	return MAX(authored_z, terrain_z + SC2_HARD_TILE_Z_BIAS);
}

static inline VECTOR3 r_sc2_hard_tile_curve_point(sc2MapHardTile_t const *a, sc2MapHardTile_t const *b, FLOAT t) {
	VECTOR3 p1 = Vector3_add(&a->position, &a->end), p2 = Vector3_add(&b->position, &b->start);
	VECTOR3 ab = Vector3_lerp(&a->position, &p1, t), bc = Vector3_lerp(&p1, &p2, t), cd = Vector3_lerp(&p2, &b->position, t);
	VECTOR3 abc = Vector3_lerp(&ab, &bc, t), bcd = Vector3_lerp(&bc, &cd, t);
	return Vector3_lerp(&abc, &bcd, t);
}

static inline VECTOR3 r_sc2_hard_tile_curve_tangent(sc2MapHardTile_t const *a, sc2MapHardTile_t const *b, FLOAT t) {
	VECTOR3 p1 = Vector3_add(&a->position, &a->end), p2 = Vector3_add(&b->position, &b->start);
	VECTOR3 d0 = Vector3_sub(&p1, &a->position), d1 = Vector3_sub(&p2, &p1), d2 = Vector3_sub(&b->position, &p2);
	FLOAT u = 1.0f - t;
	VECTOR3 tangent = Vector3_add(&(VECTOR3){d0.x * u * u, d0.y * u * u, d0.z * u * u},
		&(VECTOR3){d1.x * 2.0f * u * t + d2.x * t * t, d1.y * 2.0f * u * t + d2.y * t * t, d1.z * 2.0f * u * t + d2.z * t * t});
	return tangent;
}

static inline BOOL r_sc2_cliff_weld_compatible(LPCVERTEX a, DWORD a_group, LPCVERTEX b, DWORD b_group, FLOAT z_snap) {
	return a_group != b_group &&
		   (int)roundf(a->position.z / z_snap) == (int)roundf(b->position.z / z_snap) &&
		   Vector3_dot(&a->normal, &b->normal) > 0.0f;
}

#endif
