#ifndef common_terrain_mask_h
#define common_terrain_mask_h

#include "common/shared.h"

/* Game-owned per-frame datagram flags.  The low 14 bits remain the weather
 * count so older weather-only snapshots keep their existing wire shape. */
#define BZ_GAME_DATAGRAM_TERRAIN_MASK 0x4000u // bit mask; reserves the next weather-count bit for a synchronized terrain mask

typedef struct {
    USHORT width;
    USHORT height;
    USHORT first_row;
    USHORT row_count;
    USHORT payload_bytes;
    FLOAT min_x;
    FLOAT min_y;
    FLOAT cell_size;
} terrainMaskChunk_t;

_Static_assert(sizeof(terrainMaskChunk_t) == 24, "Terrain-mask datagram header must remain a compact wire record");

#endif
