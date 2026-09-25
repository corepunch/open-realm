#ifndef __wc3_common_terrain_h__
#define __wc3_common_terrain_h__

#include "common/mapinfo.h"

/* Warcraft III W3E terrain decode constants belong to the game module, not shared engine code. */
#define HEIGHT_COR (TILE_SIZE * 2) // world units; W3E layerHeight - 2 correction; used as the cliff baseline offset
#define WATER_HEIGHT_COR 80 // world units; W3E water baseline correction; used when decoding water vertices
#define DECODE_HEIGHT(x) (((x) - 0x2000) / 4) // raw W3E units; removes encoded bias and scales terrain height
#define WC3_PATH_UNWALKABLE 0x02 // pathing flags; ground movement/building placement blocked
#define WC3_PATH_UNBUILDABLE 0x08 // pathing flags; building placement blocked
#define WC3_PATH_BLIGHTED 0x20 // pathing flags; authored/runtime Undead Blight; used by WC3 placement and terrain queries

#ifdef WC3_DEBUG_BLIGHT
#define BLIGHT_LOG(...) do { fprintf(stderr, "WC3_BLIGHT "); fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define BLIGHT_LOG(...) ((void)0)
#endif

static inline BOOL WC3_ParseBlightTilesetLine(LPCSTR line, char *key_out, LPSTR path_out) {
    char key = 0;
    if (!line || !key_out || !path_out) return false;
    /* 255 is MAX_PATHLEN-1; keeps WorldEditData values inside a PATHSTR. */
    if (sscanf(line, " %c = %*[^,] , %255[^\r\n]", &key, path_out) < 2) return false;
    *key_out = key;
    return true;
}

#endif
