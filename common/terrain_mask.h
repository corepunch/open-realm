#ifndef common_terrain_mask_h
#define common_terrain_mask_h

#include "common/game_datagram.h"

#define TERRAIN_MASK_MAX_CELLS (1024u * 1024u) // cells; rejects absurd grid headers before any allocation

typedef struct {
    uint32_t width;
    uint32_t height;
    vector2_t origin;
    float cell_size;
    uint8_t *cells;
    uint32_t generation;
} terrainMask_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t first_row;
    uint16_t row_count;
    uint16_t payload_bytes;
    uint16_t reserved;
    float min_x;
    float min_y;
    float cell_size;
} terrainMaskChunk_t;

_Static_assert(sizeof(terrainMaskChunk_t) == 24, "Terrain-mask datagram header must remain a compact wire record");
_Static_assert(offsetof(terrainMaskChunk_t, min_x) == 12, "Terrain-mask chunk keeps wire offsets with explicit reserved");

static inline bool TerrainMask_CellForPoint(vector2_t origin, float cell_size, uint32_t width, uint32_t height, vector2_t const *point, uint32_t *x, uint32_t *y) {
    float fx, fy;
    if (!point || !x || !y || !width || !height || cell_size <= 0.0f) return false;
    fx = (point->x - origin.x) / cell_size; fy = (point->y - origin.y) / cell_size;
    if (fx < 0.0f || fy < 0.0f || fx >= (float)width || fy >= (float)height) return false;
    *x = MIN((uint32_t)fx, width - 1); *y = MIN((uint32_t)fy, height - 1);
    return true;
}

static inline uint8_t TerrainMask_CornerValue(uint8_t const *cells, uint32_t width, uint32_t height, uint32_t cells_per_tile, uint32_t cx, uint32_t cy) {
    uint32_t mx, my;
    if (!cells || !width || !height || !cells_per_tile) return 0;
    mx = MIN(width - 1, cx * cells_per_tile); my = MIN(height - 1, cy * cells_per_tile);
    return cells[mx + my * width] != 0;
}

static inline uint32_t TerrainMask_TileMask(uint8_t const *corners, uint32_t stride, uint32_t tx, uint32_t ty) {
    uint8_t const *c;
    if (!corners || !stride) return 0;
    c = &corners[tx + ty * stride];
    return (c[1] ? 1u : 0u) | (c[0] ? 2u : 0u) | (c[stride + 1] ? 4u : 0u) | (c[stride] ? 8u : 0u);
}

#endif
