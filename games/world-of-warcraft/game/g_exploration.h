#ifndef WOW_EXPLORATION_H
#define WOW_EXPLORATION_H

#include <stdbool.h>
#include <stdint.h>

/* AreaTable IDs used by classic terrain fit in 16 bits. Zero means no area. */
typedef struct {
    uint8_t visited[65536 / 8];
} wowExploration_t;

typedef struct {
    char texture[64];
    uint32_t map_area;
    uint32_t areas[4];
    int width, height;
    int left, top;
    uint16_t tile_width[16], tile_height[16];
} wowMapOverlay_t;

typedef struct {
    int sequence;
    int x, y, width, height;
    float u0, v0, u1, v1;
} wowMapPiece_t;

bool Wow_ExploreArea(wowExploration_t *state, uint32_t area);
bool Wow_MapOverlayDiscovered(wowExploration_t const *state, wowMapOverlay_t const *overlay);
bool Wow_MapOverlayRead(uint8_t const *data, uint32_t size, uint32_t row, wowMapOverlay_t *overlay);
uint32_t Wow_MapOverlayRows(uint8_t const *data, uint32_t size);
bool Wow_MapOverlayPiece(wowMapOverlay_t const *overlay, int index, wowMapPiece_t *piece);
void Wow_ClearMapOverlayCache(void);
bool Wow_ReadOverlayTileSize(wowMapOverlay_t *overlay, int index, char const *path);
wowMapOverlay_t *Wow_GetMapOverlays(uint32_t *count);

#endif
