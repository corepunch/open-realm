#ifndef R_SC2MAP_H
#define R_SC2MAP_H

#include "renderer/r_game.h"
#include "games/starcraft-2/common/sc2_map.h"

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
	matrix->v[0] = side.x; matrix->v[1] = side.y; matrix->v[2] = side.z;
	matrix->v[4] = along.x; matrix->v[5] = along.y; matrix->v[6] = along.z;
	matrix->v[8] = tile->normal.x * tile->scale.y; matrix->v[9] = tile->normal.y * tile->scale.y; matrix->v[10] = tile->normal.z * tile->scale.y;
	matrix->v[12] = tile->position.x + base.x; matrix->v[13] = tile->position.y + base.y; matrix->v[14] = tile->position.z + base.z;
}

static inline BOOL r_sc2_cliff_weld_compatible(LPCVERTEX a, DWORD a_group, LPCVERTEX b, DWORD b_group, FLOAT z_snap) {
	return a_group != b_group &&
		   (int)roundf(a->position.z / z_snap) == (int)roundf(b->position.z / z_snap) &&
		   Vector3_dot(&a->normal, &b->normal) > 0.0f;
}

#endif
