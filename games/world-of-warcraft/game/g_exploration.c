#include "g_exploration.h"
#include "g_wow_local.h"
#include "common/stb_dbc.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define WOW_MAP_WIDTH 1002
#define WOW_MAP_HEIGHT 668
#define WOW_OVERLAY_TILE 256

static wowMapOverlay_t *map_overlays;
static uint32_t map_overlay_count;
static bool map_overlays_loaded;

bool Wow_ExploreArea(wowExploration_t *state, uint32_t area) {
    if (!state || area == 0 || area >= sizeof(state->visited) * 8) return false;
    uint8_t *byte = &state->visited[area / 8];
    uint8_t bit = (uint8_t)(1u << (area % 8));
    if (*byte & bit) return false;
    *byte |= bit;
    return true;
}

bool Wow_MapOverlayDiscovered(wowExploration_t const *state, wowMapOverlay_t const *overlay) {
    if (!state || !overlay) return false;
    for (int i = 0; i < 4; i++) {
        uint32_t id = overlay->areas[i];
        if (id && id < sizeof(state->visited) * 8 &&
            (state->visited[id / 8] & (1u << (id % 8)))) return true;
    }
    return false;
}

/* Check the size in 64 bits before taking pointers into the classic 17-field DBC. */
static bool Wow_OverlayHeader(uint8_t const *data, uint32_t size, stbDbc_t *header) {
    return Stb_DbcValid(data, size, header) && header->fields == 17 &&
        header->record_size == 68 &&
        20u + (uint64_t)header->records * header->record_size + header->string_size <= size;
}

uint32_t Wow_MapOverlayRows(uint8_t const *data, uint32_t size) {
    stbDbc_t header;
    return Wow_OverlayHeader(data, size, &header) ? header.records : 0;
}

bool Wow_MapOverlayRead(uint8_t const *data, uint32_t size, uint32_t row, wowMapOverlay_t *overlay) {
    stbDbc_t header;
    if (!overlay || !Wow_OverlayHeader(data, size, &header) || row >= header.records) return false;
    uint8_t const *record = Stb_DbcRecordAt(Stb_DbcRecords(data), row, header.record_size);
    uint32_t offset = Stb_DbcField(&header, record, 8);
    cstring_t name = Stb_DbcString(Stb_DbcStrings(data, &header), header.string_size, offset);
    if (!name) return false;
    char const *end = memchr(name, 0, header.string_size - offset);
    if (!end || end == name || end - name >= sizeof(overlay->texture)) return false;
    for (char const *p = name; p != end; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') return false;
    }
    wowMapOverlay_t result = {0};
    result.map_area = Stb_DbcField(&header, record, 1);
    for (int i = 0; i < 4; i++) result.areas[i] = Stb_DbcField(&header, record, 2+i);
    result.width = (int32_t)Stb_DbcField(&header, record, 9);
    result.height = (int32_t)Stb_DbcField(&header, record, 10);
    result.left = (int32_t)Stb_DbcField(&header, record, 11);
    result.top = (int32_t)Stb_DbcField(&header, record, 12);
    if (!result.map_area || result.width <= 0 || result.width > 1024 ||
        result.height <= 0 || result.height > 1024 ||
        result.left >= WOW_MAP_WIDTH || result.left <= -result.width ||
        result.top >= WOW_MAP_HEIGHT || result.top <= -result.height) return false;
    memcpy(result.texture, name, end - name);
    *overlay = result;
    return true;
}

static int Wow_PieceTextureSize(int extent) {
    int size = 16;
    while (size < extent) size *= 2;
    return size;
}

/* Each part has its own power-of-two BLP allocation. Clip screen rectangles
 * against the map independently from that allocation, preserving source UVs. */
bool Wow_MapOverlayPiece(wowMapOverlay_t const *overlay, int index, wowMapPiece_t *piece) {
    if (!overlay || !piece || index < 0 || overlay->width <= 0 || overlay->width > 1024 ||
        overlay->height <= 0 || overlay->height > 1024) return false;
    int columns = (overlay->width + WOW_OVERLAY_TILE - 1) / WOW_OVERLAY_TILE;
    int rows = (overlay->height + WOW_OVERLAY_TILE - 1) / WOW_OVERLAY_TILE;
    if (index >= columns * rows) return false;
    int column_offset = (index % columns) * WOW_OVERLAY_TILE;
    int row_offset = (index / columns) * WOW_OVERLAY_TILE;
    int width = MIN(WOW_OVERLAY_TILE, overlay->width - column_offset);
    int height = MIN(WOW_OVERLAY_TILE, overlay->height - row_offset);
    int x = overlay->left + column_offset, y = overlay->top + row_offset;
    int x0 = MAX(0, x), y0 = MAX(0, y);
    int x1 = MIN(WOW_MAP_WIDTH, x + width), y1 = MIN(WOW_MAP_HEIGHT, y + height);
    if (x0 >= x1 || y0 >= y1) return false;
    float tw = overlay->tile_width[index] ? overlay->tile_width[index] : Wow_PieceTextureSize(width);
    float th = overlay->tile_height[index] ? overlay->tile_height[index] : Wow_PieceTextureSize(height);
    if (tw < width || th < height) return false;
    *piece = (wowMapPiece_t){index + 1, x0, y0, x1 - x0, y1 - y0,
        (x0 - x) / tw, (y0 - y) / th, (x1 - x) / tw, (y1 - y) / th};
    return true;
}

void Wow_ClearMapOverlayCache(void) {
    free(map_overlays);
    map_overlays = NULL;
    map_overlay_count = 0;
    map_overlays_loaded = false;
}

/* Some short edge pieces have more padding than the next power of two.
 * Consult each installed texture once, including negative results. Full-size
 * tiles already occupy their complete 256 by 256 allocation. */
bool Wow_ReadOverlayTileSize(wowMapOverlay_t *overlay, int index, char const *path) {
    int columns = (overlay->width + 255) / 256;
    int rows = (overlay->height + 255) / 256;
    if (index < 0 || index >= columns * rows || index >= 16) return false;
    if (overlay->tile_width[index]) return overlay->tile_width[index] != UINT16_MAX;
    int w = MIN(256, overlay->width - 256 * (index % columns));
    int h = MIN(256, overlay->height - 256 * (index / columns));
    if (w == 256 && h == 256) {
        overlay->tile_width[index] = 256;
        overlay->tile_height[index] = 256;
        return true;
    }
    uint32_t size = 0;
    uint8_t *blp = gi.ReadFile(path, &size);
    bool valid = false;
    if (blp && size >= 20 && !memcmp(blp, "BLP2", 4)) {
        uint32_t width = Stb_DbcRead32(blp + 12);
        uint32_t height = Stb_DbcRead32(blp + 16);
        if (width >= (unsigned)w && width <= 256 && height >= (unsigned)h && height <= 256) {
            overlay->tile_width[index] = (uint16_t)width;
            overlay->tile_height[index] = (uint16_t)height;
            valid = true;
        }
    }
    if (blp) gi.MemFree(blp);
    if (!valid) overlay->tile_width[index] = UINT16_MAX;
    return valid;
}

wowMapOverlay_t *Wow_GetMapOverlays(uint32_t *count) {
    if (!map_overlays_loaded) {
        uint32_t size = 0;
        uint8_t *data = gi.ReadFile("DBFilesClient\\WorldMapOverlay.dbc", &size);
        uint32_t rows = Wow_MapOverlayRows(data, size);
        if (rows) map_overlays = calloc(rows, sizeof(*map_overlays));
        if (map_overlays) {
            for (uint32_t i = 0; i < rows; i++) {
                if (Wow_MapOverlayRead(data, size, i, &map_overlays[map_overlay_count]))
                    map_overlay_count++;
            }
        }
        if (data) gi.MemFree(data);
        map_overlays_loaded = true;
    }
    *count = map_overlay_count;
    return map_overlays;
}
