#ifndef __wc3_common_terrain_h__
#define __wc3_common_terrain_h__

#include "common/mapinfo.h"

/* Warcraft III W3E terrain decode constants belong to the game module, not shared engine code. */
#define HEIGHT_COR (TILE_SIZE * 2) // world units; W3E layerHeight - 2 correction; used as the cliff baseline offset
#define DECODE_HEIGHT(x) (((x) - 0x2000) / 4) // raw W3E units; removes encoded bias and scales terrain height
#define WC3_PATH_BLIGHTED 0x20 // pathing flags; authored/runtime Undead Blight; used by WC3 placement and terrain queries

#ifdef WC3_DEBUG_BLIGHT
#define BLIGHT_LOG(...) do { fprintf(stderr, "WC3_BLIGHT "); fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define BLIGHT_LOG(...) ((void)0)
#endif

/* Water surface Z: the W3E water level plus the tileset's TerrainArt\Water.slk "<tileset>Sha" height, in tiles
 * (-0.7 for most tilesets, -1.5 for Outland). The game's water queries and the renderer share this decode so units
 * float on the drawn surface. */
static inline float W3_WaterSurfaceHeight(uint16_t waterlevel, float slk_height) {
    return ((float)waterlevel - 0x2000) / 4.0f + slk_height * TILE_SIZE;
}

/*749a30: subtract the minimum corner before ordered SSE-style bilinear
 * accumulation. Nested lerps give different authoritative support words. */
static inline float W3_InterpolateGroundHeight(float x,float y,float const h[4]) {
    float minimum=h[0];
    for(unsigned i=1;i<4;i++)if(h[i]<minimum)minimum=h[i];
    volatile float nx=1-x,ny=1-y;
    volatile float a=h[0]-minimum,b=h[1]-minimum,c=h[2]-minimum,d=h[3]-minimum;
    a=a*ny;a=a*nx;a=a+minimum;
    b=b*ny;b=b*x;a=a+b;
    c=c*y;c=c*nx;a=a+c;
    d=d*y;d=d*x;a=a+d;
    a=a+0.0f;
    return a;
}

/* Collision-world side: the game applies the loaded map's Water.slk height. */
void CM_W3SetWaterHeight(float slk_height);
float CM_W3WaterHeight(void);

static inline bool WC3_ParseBlightTilesetLine(cstring_t line, char *key_out, string_t path_out) {
    char key = 0;
    if (!line || !key_out || !path_out) return false;
    /* 255 is MAX_PATHLEN-1; keeps WorldEditData values inside a PATHSTR. */
    if (sscanf(line, " %c = %*[^,] , %255[^\r\n]", &key, path_out) < 2) return false;
    *key_out = key;
    return true;
}

#endif
