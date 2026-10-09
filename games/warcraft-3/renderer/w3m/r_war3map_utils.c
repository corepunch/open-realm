#include "r_war3map.h"

war3mapVertex_t const *GetWar3MapVertex(war3map_t const *war3Map, uint32_t x, uint32_t y) {
    x = MIN(x, war3Map->width - 1);
    y = MIN(y, war3Map->height - 1);
    int const index = x + y * war3Map->width;
    char const *ptr = ((char const *)war3Map->vertices) + index * sizeof(war3mapVertex_t);
    return (war3mapVertex_t const *)ptr;
}

vec2_t GetWar3MapSize(war3map_t const *war3Map) {
    vec2_t size = {
        .x = (tr.world->width - 1) * TILE_SIZE,
        .y = (tr.world->height - 1) * TILE_SIZE
    };
    return size;
}

vec2_t GetWar3MapPosition(war3map_t const *war3Map, float x, float y) {
    vec2_t size = GetWar3MapSize(war3Map);
    vec2_t point = {
        .x = (x - war3Map->center.x) / size.x,
        .y = (y - war3Map->center.y) / size.y,
    };
    return point;
}

float GetTileDepth(float waterlevel, float height) {
    float const opacity = MIN(0.95, (waterlevel - height) / 250.0f);
    return 1 - MAX(0, opacity);
}

struct color32 MakeColor(float r, float g, float b, float a) {
    return (struct color32) {
        .r = r * 255,
        .g = g * 255,
        .b = b * 255,
        .a = a * 255,
    };
}

/* Water.slk colors interpolate across two depth bands; depth is in tiles below the water surface.
 * Band edges follow HiveWE/Warsmash: shallow from 10/128 to 64/128, deep from 64/128 to 72/128. */
color32_t R_WaterDepthColor(wc3WaterStyle_t const *style, float depth) {
    float const min_depth = 10.f / 128, deep_level = 64.f / 128, max_depth = 72.f / 128;
    color32_t const *from = &style->shallow_min, *to = &style->shallow_max;
    float t;
    depth = MIN(MAX(depth, 0), 1);
    if (depth <= deep_level) {
        t = MAX(0, depth - min_depth) / (deep_level - min_depth);
    } else {
        from = &style->deep_min; to = &style->deep_max;
        t = MIN(depth - deep_level, max_depth - deep_level) / (max_depth - deep_level);
    }
    return (color32_t) {
        .r = (uint8_t)lroundf(LerpNumber(from->r, to->r, t)),
        .g = (uint8_t)lroundf(LerpNumber(from->g, to->g, t)),
        .b = (uint8_t)lroundf(LerpNumber(from->b, to->b, t)),
        .a = (uint8_t)lroundf(LerpNumber(from->a, to->a, t)),
    };
}

/* Tile atlases are four cells tall; an extended atlas is twice as wide and adds 16 variations on its right
 * half. The cell size follows the texture, not a fixed 64 pixels: Outland_Abyss is a 64x64 4x4 atlas. */
void SetTileUV(war3mapVertex_t const *mv, uint32_t tile, vertex_t *vertices, texture_t const *texture) {
    float u = texture->width > texture->height ? 0.125f : 0.25f;
    float v = 0.25f;
    float ux = 0.0f;
    
    if (tile == 15 && texture->width > texture->height) {
        if (mv->groundVariation <= 15) {
            tile = mv->groundVariation;
            ux = 0.5f;
        } else if (mv->groundVariation == 16) {
            tile = 15;
        } else {
            tile = 0;
        }
    }

    vertices[0].texcoord.x = u * ((tile%4)+0)+ux;
    vertices[0].texcoord.y = v * ((tile/4)+1);
    vertices[1].texcoord.x = u * ((tile%4)+1)+ux;
    vertices[1].texcoord.y = v * ((tile/4)+1);
    vertices[2].texcoord.x = u * ((tile%4)+1)+ux;
    vertices[2].texcoord.y = v * ((tile/4)+0);
    vertices[3].texcoord.x = u * ((tile%4)+0)+ux;
    vertices[3].texcoord.y = v * ((tile/4)+1);
    vertices[4].texcoord.x = u * ((tile%4)+1)+ux;
    vertices[4].texcoord.y = v * ((tile/4)+0);
    vertices[5].texcoord.x = u * ((tile%4)+0)+ux;
    vertices[5].texcoord.y = v * ((tile/4)+0);
    
    FOR_LOOP(i, 6) {
        vertices[i].texcoord.x = LerpNumber(vertices[i].texcoord.x, u * ((tile%4)+0.5)+ux, 0.05);
        vertices[i].texcoord.y = LerpNumber(vertices[i].texcoord.y, v * ((tile/4)+0.5), 0.05);
    }
}

uint32_t GetTile(war3mapVertex_t const *mv, uint32_t ground) {
    if (ground == 0)
        return 15;
    return
        (mv[0].ground >= ground ? 4 : 0) +
        (mv[1].ground >= ground ? 8 : 0) +
        (mv[2].ground >= ground ? 1 : 0) +
        (mv[3].ground >= ground ? 2 : 0);
}

float GetWar3MapVertexHeight(war3mapVertex_t const *vert) {
    return DECODE_HEIGHT(vert->accurate_height) + vert->level * TILE_SIZE - HEIGHT_COR;
}

float GetWar3MapVertexWaterLevel(war3mapVertex_t const *vert) {
    return W3_WaterSurfaceHeight(vert->waterlevel, R_WaterStyle()->height);
}

void GetTileVertices(uint32_t x, uint32_t y, war3map_t const *war3Map, war3mapVertex_t *vertices) {
    vertices[0] = *GetWar3MapVertex(war3Map, x+1, y+1);
    vertices[1] = *GetWar3MapVertex(war3Map, x, y+1);
    vertices[2] = *GetWar3MapVertex(war3Map, x+1, y);
    vertices[3] = *GetWar3MapVertex(war3Map, x, y);
}

uint32_t GetTileRamps(war3mapVertex_t const *vertices) {
    return vertices[0].ramp + vertices[1].ramp + vertices[2].ramp + vertices[3].ramp;
}

uint32_t IsTileCliff(war3mapVertex_t const *vertices) {
    int bIsCliff = 0;
    FOR_LOOP(index, 4) {
        bIsCliff |= vertices[index].level != vertices[0].level;
    }
    return bIsCliff;
}

uint32_t IsTileWater(war3mapVertex_t const *vertices) {
    int bIsWater = 0;
    FOR_LOOP(index, 4) {
        bIsWater |= vertices[index].water;
    }
    FOR_LOOP(index, 4) {
        bIsWater &= !vertices[index].mapedge;
    }
    return bIsWater;
}
