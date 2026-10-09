#include "r_war3map.h"

static vertex_t water_vertex_buffer[(SEGMENT_SIZE+1)*(SEGMENT_SIZE+1)*6];
static vertex_t *water_current_vertex = NULL;

// FUNCTIONS

static void R_MakeWaterTile(war3map_t const *map, uint32_t x, uint32_t y) {
    struct War3MapVertex tile[4];
    GetTileVertices(x, y, tr.world, tile);

    if (!IsTileWater(tile))
        return;

    vec2_t const pos[] = {
        { tr.world->center.x + x * TILE_SIZE, tr.world->center.y + y * TILE_SIZE },
        { tr.world->center.x + (x + 1) * TILE_SIZE, tr.world->center.y + y * TILE_SIZE },
        { tr.world->center.x + (x + 1) * TILE_SIZE, tr.world->center.y + (y + 1) * TILE_SIZE },
        { tr.world->center.x + x * TILE_SIZE, tr.world->center.y + (y + 1) * TILE_SIZE },
    };

    float const waterlevel[] = {
        GetWar3MapVertexWaterLevel(&tile[3]),
        GetWar3MapVertexWaterLevel(&tile[2]),
        GetWar3MapVertexWaterLevel(&tile[0]),
        GetWar3MapVertexWaterLevel(&tile[1]),
    };

    float const height[] = {
        GetWar3MapVertexHeight(&tile[3]),
        GetWar3MapVertexHeight(&tile[2]),
        GetWar3MapVertexHeight(&tile[0]),
        GetWar3MapVertexHeight(&tile[1]),
    };

    wc3WaterStyle_t const *style = R_WaterStyle();
    struct color32 const color[] = {
        R_WaterDepthColor(style, (waterlevel[0] - height[0]) / TILE_SIZE),
        R_WaterDepthColor(style, (waterlevel[1] - height[1]) / TILE_SIZE),
        R_WaterDepthColor(style, (waterlevel[2] - height[2]) / TILE_SIZE),
        R_WaterDepthColor(style, (waterlevel[3] - height[3]) / TILE_SIZE),
    };
    
#define WATER_SCALE(x,y) (((x%3)+y)/3.0)
    
    vec2_t const tc[] = {
        { WATER_SCALE(x, 0),  WATER_SCALE(y, 0) },
        { WATER_SCALE(x, 1),  WATER_SCALE(y, 0) },
        { WATER_SCALE(x, 1),  WATER_SCALE(y, 1) },
        { WATER_SCALE(x, 0),  WATER_SCALE(y, 1) },
    };

    struct vertex geom[] = {
        {
            .position = { pos[0].x, pos[0].y, waterlevel[0] },
            .texcoord = tc[0],
            .normal = { 0, 0, 1 },
            .color = color[0],
        },
        {
            .position = { pos[1].x, pos[1].y, waterlevel[1] },
            .texcoord = tc[1],
            .normal = { 0, 0, 1 },
            .color = color[1],
        },
        {
            .position = { pos[2].x, pos[2].y, waterlevel[2] },
            .texcoord = tc[2],
            .normal = { 0, 0, 1 },
            .color = color[2],
        },
        {
            .position = { pos[0].x, pos[0].y, waterlevel[0] },
            .texcoord = tc[0],
            .normal = { 0, 0, 1 },
            .color = color[0],
        },
        {
            .position = { pos[2].x, pos[2].y, waterlevel[2] },
            .texcoord = tc[2],
            .normal = { 0, 0, 1 },
            .color = color[2],
        },
        {
            .position = { pos[3].x, pos[3].y, waterlevel[3] },
            .texcoord = tc[3],
            .normal = { 0, 0, 1 },
            .color = color[3],
        },
    };

    memcpy(water_current_vertex, geom, sizeof(geom));
    water_current_vertex += sizeof(geom) / sizeof(vertex_t);
}

maplayer_t *R_BuildMapSegmentWater(war3map_t const *map, uint32_t sx, uint32_t sy) {
    /* R_LoadWaterStyle already reported a missing Water.slk row or texture for this tileset. */
    if (!R_WaterStyle()->num_frames) return NULL;
    maplayer_t *mapLayer = ri.MemAlloc(sizeof(maplayer_t));
    mapLayer->type = MAPLAYERTYPE_WATER;
    mapLayer->texture = R_WaterStyle()->frames[0]; /* _W3M_DrawAlphaSurfaces selects the animated frame. */
    water_current_vertex = water_vertex_buffer;
    for (uint32_t x = sx * SEGMENT_SIZE; x < (sx + 1) * SEGMENT_SIZE; x++) {
        for (uint32_t y = sy * SEGMENT_SIZE; y < (sy + 1) * SEGMENT_SIZE; y++) {
            R_MakeWaterTile(map, x, y);
        }
    }
    mapLayer->num_vertices = (uint32_t)(water_current_vertex - water_vertex_buffer);
    mapLayer->buffer = R_MakeVertexArrayObject(water_vertex_buffer, mapLayer->num_vertices);
    return mapLayer;
}
