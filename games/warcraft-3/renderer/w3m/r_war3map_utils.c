#include "r_war3map.h"

LPCWAR3MAPVERTEX GetWar3MapVertex(LPCWAR3MAP war3Map, DWORD x, DWORD y) {
    x = MIN(x, war3Map->width - 1);
    y = MIN(y, war3Map->height - 1);
    int const index = x + y * war3Map->width;
    char const *ptr = ((char const *)war3Map->vertices) + index * sizeof(WAR3MAPVERTEX);
    return (LPCWAR3MAPVERTEX)ptr;
}

VECTOR2 GetWar3MapSize(LPCWAR3MAP war3Map) {
    VECTOR2 size = {
        .x = (tr.world->width - 1) * TILE_SIZE,
        .y = (tr.world->height - 1) * TILE_SIZE
    };
    return size;
}

VECTOR2 GetWar3MapPosition(LPCWAR3MAP war3Map, float x, float y) {
    VECTOR2 size = GetWar3MapSize(war3Map);
    VECTOR2 point = {
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

void SetTileUV(LPCWAR3MAPVERTEX mv, DWORD tile, LPVERTEX vertices, LPCTEXTURE texture) {
    float u = 1.f/(texture->width / 64);
    float v = 1.f/(texture->height / 64);
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

DWORD GetTile(LPCWAR3MAPVERTEX mv, DWORD ground) {
    if (ground == 0)
        return 15;
    return
        (mv[0].ground >= ground ? 4 : 0) +
        (mv[1].ground >= ground ? 8 : 0) +
        (mv[2].ground >= ground ? 1 : 0) +
        (mv[3].ground >= ground ? 2 : 0);
}

float GetWar3MapVertexHeight(LPCWAR3MAPVERTEX vert) {
    return DECODE_HEIGHT(vert->accurate_height) + vert->level * TILE_SIZE - HEIGHT_COR;
}

float GetWar3MapVertexWaterLevel(LPCWAR3MAPVERTEX vert) {
    return DECODE_HEIGHT(vert->waterlevel) - WATER_HEIGHT_COR;
}

void GetTileVertices(DWORD x, DWORD y, LPCWAR3MAP war3Map, LPWAR3MAPVERTEX vertices) {
    vertices[0] = *GetWar3MapVertex(war3Map, x+1, y+1);
    vertices[1] = *GetWar3MapVertex(war3Map, x, y+1);
    vertices[2] = *GetWar3MapVertex(war3Map, x+1, y);
    vertices[3] = *GetWar3MapVertex(war3Map, x, y);
}

DWORD GetTileRamps(LPCWAR3MAPVERTEX vertices) {
    return vertices[0].ramp + vertices[1].ramp + vertices[2].ramp + vertices[3].ramp;
}

DWORD IsTileCliff(LPCWAR3MAPVERTEX vertices) {
    int bIsCliff = 0;
    FOR_LOOP(index, 4) {
        bIsCliff |= vertices[index].level != vertices[0].level;
    }
    return bIsCliff;
}

DWORD IsTileWater(LPCWAR3MAPVERTEX vertices) {
    int bIsWater = 0;
    FOR_LOOP(index, 4) {
        bIsWater |= vertices[index].water;
    }
    FOR_LOOP(index, 4) {
        bIsWater &= !vertices[index].mapedge;
    }
    return bIsWater;
}
