#ifndef WC3_SC2API_MAP_H
#define WC3_SC2API_MAP_H

#include "sc2api_compat.h"

/* Protocol-independent SC2 ImageData shape. Callers own the byte buffers;
 * this layer only writes authoritative Warcraft map state into them. */
typedef struct {
    uint32_t bits_per_pixel;
    int32_t width;
    int32_t height;
    uint32_t data_size;
    uint8_t *data;
} wc3Sc2ImageData_t;

typedef struct {
    bool has_visibility;
    wc3Sc2ImageData_t visibility;
    bool has_creep;
    wc3Sc2ImageData_t creep; /* Compatibility alias: Warcraft Blight. */
} wc3Sc2MapState_t;

typedef struct {
    int32_t x, y;
} wc3Sc2Size2DI_t;

typedef struct {
    int32_t x, y;
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
    vec2_t start_locations[MAX_PLAYERS];
    uint32_t start_location_count;
} wc3Sc2StartRaw_t;

/* SC2 board coordinates are anchored at the lower-left map edge. One board
 * unit corresponds to one Warcraft pathing cell, keeping StartRaw.map_size,
 * raw unit positions and point actions in one coherent coordinate space. */
float WC3_SC2API_BoardCellWorldSize(void);
vec2_t WC3_SC2API_WorldToBoardPoint(vec2_t const *point);
vec2_t WC3_SC2API_BoardToWorldPoint(vec2_t const *point);
float WC3_SC2API_WorldToBoardDistance(float distance);
float WC3_SC2API_BoardToWorldDistance(float distance);
float WC3_SC2API_WorldToBoardHeight(float height);
float WC3_SC2API_BoardToWorldHeight(float height);
uint32_t WC3_SC2API_MapWidth(void);
uint32_t WC3_SC2API_MapHeight(void);

uint32_t WC3_SC2API_PathingDataSize(void);
uint32_t WC3_SC2API_TerrainHeightDataSize(void);
uint32_t WC3_SC2API_PlacementDataSize(void);
bool WC3_SC2API_FillStartRaw(uint8_t *pathing_data, uint32_t pathing_capacity,
                             uint8_t *terrain_height_data, uint32_t terrain_height_capacity,
                             uint8_t *placement_data, uint32_t placement_capacity,
                             wc3Sc2StartRaw_t *out);

uint32_t WC3_SC2API_VisibilityDataSize(void);
uint32_t WC3_SC2API_CreepDataSize(void);
bool WC3_SC2API_FillMapState(uint32_t player,
                             uint8_t *visibility_data, uint32_t visibility_capacity,
                             uint8_t *creep_data, uint32_t creep_capacity,
                             wc3Sc2MapState_t *out);

#endif
