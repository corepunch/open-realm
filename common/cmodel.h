#ifndef war3map_h
#define war3map_h

#include "common.h"

struct War3MapVertex {
    USHORT accurate_height;
    USHORT waterlevel;
    BYTE mapedge;
    BYTE ground;
    BYTE ramp;
    BYTE blight;
    BYTE water;
    BYTE boundary;
    BYTE groundVariation;
    BYTE cliffVariation; // used also to mark mid-ramp
    BYTE level;
    BYTE cliff;
};

struct war3map {
    DWORD header;
    DWORD version;
    BYTE tileset;
    DWORD custom;
    LPDWORD grounds;
    LPDWORD cliffs;
    VECTOR2 center;
    DWORD width;
    DWORD height;
    HANDLE vertices;
    DWORD num_grounds;
    DWORD num_cliffs;
};

bool CM_LoadMap(LPCSTR mapFilename);
BOOL CM_IsMapLoaded(LPCSTR mapFilename);
float CM_GetHeightAtPoint(float sx, float sy);
LPDOODAD CM_GetDoodads(void);
//LPCMAPPLAYER CM_GetPlayer(DWORD index);
DWORD CM_GetLocalPlayerNumber(void);
LPCMAPINFO CM_GetMapInfo(void);
VECTOR2 CM_GetNormalizedMapPosition(float x, float y);
VECTOR2 CM_GetDenormalizedMapPosition(float x, float y);
BOOL CM_ClosestPathablePoint(LPCVECTOR2 location, LPVECTOR2 out);
BOOL CM_ClosestPathablePointForRadius(LPCVECTOR2 location, FLOAT radius, LPVECTOR2 out);
BOX2 CM_GetWorldBounds(void);
void CM_BakeStaticObstacles(void);
void CM_InvalidatePathCache(void);
void CM_SetupPathMap(DWORD width, DWORD height, BYTE const *cells);

#endif
