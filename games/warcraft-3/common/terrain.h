#ifndef __wc3_common_terrain_h__
#define __wc3_common_terrain_h__

#include "common/mapinfo.h"

/* Warcraft III W3E terrain decode constants belong to the game module, not shared engine code. */
#define HEIGHT_COR (TILE_SIZE * 2) // world units; W3E layerHeight - 2 correction; used as the cliff baseline offset
#define WATER_HEIGHT_COR 80 // world units; W3E water baseline correction; used when decoding water vertices
#define DECODE_HEIGHT(x) (((x) - 0x2000) / 4) // raw W3E units; removes encoded bias and scales terrain height
#define WC3_PATH_BLIGHTED 0x20 // pathing flags; authored/runtime Undead Blight; used by WC3 placement and terrain queries

#ifdef WC3_DEBUG_BLIGHT
#define BLIGHT_LOG(...) do { fprintf(stderr, "WC3_BLIGHT "); fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define BLIGHT_LOG(...) ((void)0)
#endif

#endif
