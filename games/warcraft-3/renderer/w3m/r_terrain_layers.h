#ifndef R_TERRAIN_LAYERS_H
#define R_TERRAIN_LAYERS_H

#include "renderer/r_local.h"

KNOWN_AS(MapLayer, maplayer_t);
KNOWN_AS(MapSegment, mapsegment_t);

typedef struct terrainNormals_s {
    void const *data;
    float (*height)(void const *data, uint32_t x, uint32_t y);
    uint32_t width, height_count;
    float cell_size;
} terrainNormals_t;



typedef enum {
    MAPLAYERTYPE_GROUND,
    MAPLAYERTYPE_CLIFF,
    MAPLAYERTYPE_WATER,
} MAPLAYERTYPE;

struct MapLayer {
    MAPLAYERTYPE type;
    buffer_t const *buffer;
    texture_t const *texture;
    maplayer_t *next;
    uint32_t num_vertices;
    uint32_t num_indices;
};

struct MapSegment {
    maplayer_t *layers;
    mapsegment_t *next;
    box3_t bbox;
};

void R_DrawTerrainSegment(mapsegment_t const *segment, uint32_t mask);

/* Both tile renderers need normals independent of triangle diagonals and holes in neighbouring cells. */
static inline vec3_t R_TerrainGridNormal(terrainNormals_t const *grid, uint32_t x, uint32_t y) {
    float left = grid->height(grid->data, x ? x - 1 : x, y);
    float right = grid->height(grid->data, x + (x + 1 < grid->width), y);
    float top = grid->height(grid->data, x, y ? y - 1 : y);
    float bottom = grid->height(grid->data, x, y + (y + 1 < grid->height_count));
    vec3_t dx = { x && x + 1 < grid->width ? 2.0f * grid->cell_size : grid->cell_size, 0.0f, right - left };
    vec3_t dy = { 0.0f, y && y + 1 < grid->height_count ? 2.0f * grid->cell_size : grid->cell_size, bottom - top };
    vec3_t normal = Vector3_cross(&dx, &dy);

    Vector3_normalize(&normal);
    return normal;
}

#endif
