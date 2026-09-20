#ifndef common_terrain_mask_h
#define common_terrain_mask_h

#include "common/game_datagram.h"

typedef struct {
    DWORD width;
    DWORD height;
    VECTOR2 origin;
    FLOAT cell_size;
    BYTE *cells;
    DWORD generation;
} terrainMask_t;

typedef struct {
    USHORT width;
    USHORT height;
    USHORT first_row;
    USHORT row_count;
    USHORT payload_bytes;
    USHORT reserved;
    FLOAT min_x;
    FLOAT min_y;
    FLOAT cell_size;
} terrainMaskChunk_t;

_Static_assert(sizeof(terrainMaskChunk_t) == 24, "Terrain-mask datagram header must remain a compact wire record");
_Static_assert(offsetof(terrainMaskChunk_t, min_x) == 12, "Terrain-mask chunk keeps wire offsets with explicit reserved");

static inline BOOL TerrainMask_CellForPoint(VECTOR2 origin, FLOAT cell_size, DWORD width, DWORD height, LPCVECTOR2 point, LPDWORD x, LPDWORD y) {
    FLOAT fx, fy;
    if (!point || !x || !y || !width || !height || cell_size <= 0.0f) return false;
    fx = (point->x - origin.x) / cell_size; fy = (point->y - origin.y) / cell_size;
    if (fx < 0.0f || fy < 0.0f || fx >= (FLOAT)width || fy >= (FLOAT)height) return false;
    *x = MIN((DWORD)fx, width - 1); *y = MIN((DWORD)fy, height - 1);
    return true;
}

#endif
