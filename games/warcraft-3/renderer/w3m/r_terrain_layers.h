#ifndef R_TERRAIN_LAYERS_H
#define R_TERRAIN_LAYERS_H

#include "renderer/r_local.h"

KNOWN_AS(MapLayer, MAPLAYER);
KNOWN_AS(MapSegment, MAPSEGMENT);

typedef struct TERRAINNORMALS {
    LPCVOID data;
    FLOAT (*height)(LPCVOID data, DWORD x, DWORD y);
    DWORD width, height_count;
    FLOAT cell_size;
} TERRAINNORMALS;
typedef struct TERRAINNORMALS *LPTERRAINNORMALS;
typedef const struct TERRAINNORMALS *LPCTERRAINNORMALS;

typedef enum {
    MAPLAYERTYPE_GROUND,
    MAPLAYERTYPE_CLIFF,
    MAPLAYERTYPE_WATER,
} MAPLAYERTYPE;

struct MapLayer {
    MAPLAYERTYPE type;
    LPCBUFFER buffer;
    LPCTEXTURE texture;
    LPMAPLAYER next;
    DWORD num_vertices;
    DWORD num_indices;
};

struct MapSegment {
    LPMAPLAYER layers;
    LPMAPSEGMENT next;
    BOX3 bbox;
};

void R_DrawTerrainSegment(LPCMAPSEGMENT segment, DWORD mask);

/* Both tile renderers need normals independent of triangle diagonals and holes in neighbouring cells. */
static inline VECTOR3 R_TerrainGridNormal(LPCTERRAINNORMALS grid, DWORD x, DWORD y) {
    FLOAT left = grid->height(grid->data, x ? x - 1 : x, y);
    FLOAT right = grid->height(grid->data, x + (x + 1 < grid->width), y);
    FLOAT top = grid->height(grid->data, x, y ? y - 1 : y);
    FLOAT bottom = grid->height(grid->data, x, y + (y + 1 < grid->height_count));
    VECTOR3 dx = { x && x + 1 < grid->width ? 2.0f * grid->cell_size : grid->cell_size, 0.0f, right - left };
    VECTOR3 dy = { 0.0f, y && y + 1 < grid->height_count ? 2.0f * grid->cell_size : grid->cell_size, bottom - top };
    VECTOR3 normal = Vector3_cross(&dx, &dy);

    Vector3_normalize(&normal);
    return normal;
}

#endif
