#ifndef WC3_SC2API_MAP_H
#define WC3_SC2API_MAP_H

#include "sc2api_compat.h"

/* Protocol-independent SC2 ImageData shape. Callers own the byte buffers;
 * this layer only writes authoritative Warcraft map state into them. */
typedef struct {
    DWORD bits_per_pixel;
    LONG width;
    LONG height;
    DWORD data_size;
    LPBYTE data;
} wc3Sc2ImageData_t;

typedef struct {
    BOOL has_visibility;
    wc3Sc2ImageData_t visibility;
    BOOL has_creep;
    wc3Sc2ImageData_t creep; /* Compatibility alias: Warcraft Blight. */
} wc3Sc2MapState_t;

typedef struct {
    LONG x, y;
} wc3Sc2Size2DI_t;

typedef struct {
    LONG x, y;
} wc3Sc2PointI_t;

typedef struct {
    wc3Sc2PointI_t p0, p1;
} wc3Sc2RectangleI_t;

typedef struct {
    wc3Sc2Size2DI_t map_size;
    wc3Sc2ImageData_t pathing_grid;
    wc3Sc2ImageData_t terrain_height;
    wc3Sc2ImageData_t placement_grid;
    wc3Sc2RectangleI_t playable_area;
    VECTOR2 start_locations[MAX_PLAYERS];
    DWORD start_location_count;
} wc3Sc2StartRaw_t;

/* SC2 board coordinates are anchored at the lower-left map edge. One board
 * unit corresponds to one Warcraft pathing cell, keeping StartRaw.map_size,
 * raw unit positions and point actions in one coherent coordinate space. */
FLOAT WC3_SC2API_BoardCellWorldSize(void);
VECTOR2 WC3_SC2API_WorldToBoardPoint(LPCVECTOR2 point);
VECTOR2 WC3_SC2API_BoardToWorldPoint(LPCVECTOR2 point);
FLOAT WC3_SC2API_WorldToBoardDistance(FLOAT distance);
FLOAT WC3_SC2API_BoardToWorldDistance(FLOAT distance);
FLOAT WC3_SC2API_WorldToBoardHeight(FLOAT height);
FLOAT WC3_SC2API_BoardToWorldHeight(FLOAT height);
DWORD WC3_SC2API_MapWidth(void);
DWORD WC3_SC2API_MapHeight(void);

DWORD WC3_SC2API_PathingDataSize(void);
DWORD WC3_SC2API_TerrainHeightDataSize(void);
DWORD WC3_SC2API_PlacementDataSize(void);
BOOL WC3_SC2API_FillStartRaw(LPBYTE pathing_data, DWORD pathing_capacity,
                             LPBYTE terrain_height_data, DWORD terrain_height_capacity,
                             LPBYTE placement_data, DWORD placement_capacity,
                             wc3Sc2StartRaw_t *out);

DWORD WC3_SC2API_VisibilityDataSize(void);
DWORD WC3_SC2API_CreepDataSize(void);
BOOL WC3_SC2API_FillMapState(DWORD player,
                             LPBYTE visibility_data, DWORD visibility_capacity,
                             LPBYTE creep_data, DWORD creep_capacity,
                             wc3Sc2MapState_t *out);

#endif
