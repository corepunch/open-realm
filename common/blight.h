#ifndef common_blight_h
#define common_blight_h

#include "common/shared.h"

/* Game-owned per-frame datagram flags.  The low 14 bits remain the weather
 * count so older weather-only snapshots keep their existing wire shape. */
#define BZ_GAME_DATAGRAM_BLIGHT 0x4000u

typedef struct {
    USHORT width;
    USHORT height;
    USHORT first_row;
    USHORT row_count;
    USHORT payload_bytes;
    FLOAT min_x;
    FLOAT min_y;
    FLOAT cell_size;
} wc3BlightChunk_t;

_Static_assert(sizeof(wc3BlightChunk_t) == 24, "Blight datagram header must remain a compact wire record");

#endif
