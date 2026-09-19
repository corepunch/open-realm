#include "g_local.h"

#define WC3_BLIGHT_PATH_CELL 32.0f
#define WC3_BLIGHT_TERRAIN_CELL 128.0f

static DWORD G_BlightCellCount(void) { return level.blight.width * level.blight.height; }

static BOOL G_BlightCell(LPCVECTOR2 point, LPDWORD x, LPDWORD y) {
    FLOAT fx, fy;
    if (!point || !level.blight.cells || !level.blight.width || !level.blight.height) return false;
    if (point->x < level.blight.bounds.min.x || point->y < level.blight.bounds.min.y ||
        point->x >= level.blight.bounds.max.x || point->y >= level.blight.bounds.max.y) return false;
    fx = (point->x - level.blight.bounds.min.x) / WC3_BLIGHT_PATH_CELL;
    fy = (point->y - level.blight.bounds.min.y) / WC3_BLIGHT_PATH_CELL;
    *x = MIN((DWORD)fx, level.blight.width - 1); *y = MIN((DWORD)fy, level.blight.height - 1);
    return true;
}

static void G_SetBlightCell(LONG x, LONG y, BOOL add) {
    DWORD index;
    if (x < 0 || y < 0 || x >= (LONG)level.blight.width || y >= (LONG)level.blight.height) return;
    index = (DWORD)x + (DWORD)y * level.blight.width;
    level.blight.cells[index] = add ? 1 : 0;
}

static void G_SetBlightCorner(FLOAT x, FLOAT y, BOOL add) {
    VECTOR2 sample;
    DWORD ux, uy;
    LONG cx, cy;
    sample.x = x; sample.y = y;
    if (!G_BlightCell(&sample, &ux, &uy)) return;
    /* One 128-unit terrain corner owns the surrounding 4x4 32-unit pathing cells. */
    cx = (LONG)ux - 2; cy = (LONG)uy - 2;
    FOR_LOOP(ix, 4) FOR_LOOP(iy, 4) G_SetBlightCell(cx + (LONG)ix, cy + (LONG)iy, add);
}

static FLOAT G_SnapBlightCorner(FLOAT value, FLOAT minimum) {
    return minimum + floorf((value - minimum) / WC3_BLIGHT_TERRAIN_CELL) * WC3_BLIGHT_TERRAIN_CELL;
}

void G_BlightShutdown(void) {
    SAFE_DELETE(level.blight.cells, gi.MemFree);
    memset(&level.blight, 0, sizeof(level.blight));
}

void G_BlightInit(void) {
    DWORD cells;
    VECTOR2 sample;

    G_BlightShutdown();
    level.blight.bounds = CM_GetWorldBounds();
    level.blight.width = MAX(1u, (DWORD)ceilf((level.blight.bounds.max.x - level.blight.bounds.min.x) / WC3_BLIGHT_PATH_CELL));
    level.blight.height = MAX(1u, (DWORD)ceilf((level.blight.bounds.max.y - level.blight.bounds.min.y) / WC3_BLIGHT_PATH_CELL));
    cells = G_BlightCellCount();
    level.blight.cells = gi.MemAlloc(cells);
    if (!level.blight.cells) {
        fprintf(stderr, "G_BlightInit: failed to allocate %u-cell Blight grid\n", (unsigned)cells);
        G_BlightShutdown(); return;
    }
    memset(level.blight.cells, 0, cells);
    FOR_LOOP(y, level.blight.height) FOR_LOOP(x, level.blight.width) {
        BYTE flags = 0;
        DWORD const index = x + y * level.blight.width;
        sample.x = level.blight.bounds.min.x + ((FLOAT)x + 0.5f) * WC3_BLIGHT_PATH_CELL;
        sample.y = level.blight.bounds.min.y + ((FLOAT)y + 0.5f) * WC3_BLIGHT_PATH_CELL;
        if (!CM_GetPathingFlagsAt(&sample, &flags)) {
            fprintf(stderr, "WC3 Blight: pathing flags unavailable at (%.1f,%.1f); leaving cell clear\n",
                    sample.x, sample.y);
            continue;
        }
        level.blight.cells[index] = (flags & WC3_PATH_BLIGHTED) != 0;
    }
}

BOOL G_IsPointBlighted(LPCVECTOR2 point) {
    DWORD x, y;
    return G_BlightCell(point, &x, &y) && level.blight.cells[x + y * level.blight.width] != 0;
}

void G_SetBlightPoint(LPCVECTOR2 point, BOOL add) {
    FLOAT x, y;
    if (!point || !level.blight.cells) return;
    x = G_SnapBlightCorner(point->x, level.blight.bounds.min.x);
    y = G_SnapBlightCorner(point->y, level.blight.bounds.min.y);
    G_SetBlightCorner(x, y, add);
}

void G_SetBlightRadius(LPCVECTOR2 point, FLOAT radius, BOOL add) {
    FLOAT min_x, min_y, max_x, max_y;
    if (!point || !level.blight.cells || radius < 0.0f) return;
    min_x = G_SnapBlightCorner(point->x - radius, level.blight.bounds.min.x);
    min_y = G_SnapBlightCorner(point->y - radius, level.blight.bounds.min.y);
    max_x = point->x + radius; max_y = point->y + radius;
    for (FLOAT y = min_y; y <= max_y; y += WC3_BLIGHT_TERRAIN_CELL)
        for (FLOAT x = min_x; x <= max_x; x += WC3_BLIGHT_TERRAIN_CELL) {
            FLOAT const dx = x - point->x, dy = y - point->y;
            if (dx * dx + dy * dy <= radius * radius) G_SetBlightCorner(x, y, add);
        }
}

void G_SetBlightRect(LPCBOX2 rect, BOOL add) {
    FLOAT min_x, min_y;
    if (!rect || !level.blight.cells) return;
    min_x = G_SnapBlightCorner(rect->min.x, level.blight.bounds.min.x);
    min_y = G_SnapBlightCorner(rect->min.y, level.blight.bounds.min.y);
    for (FLOAT y = min_y; y <= rect->max.y; y += WC3_BLIGHT_TERRAIN_CELL)
        for (FLOAT x = min_x; x <= rect->max.x; x += WC3_BLIGHT_TERRAIN_CELL)
            if (x >= rect->min.x && y >= rect->min.y) G_SetBlightCorner(x, y, add);
}

DWORD G_GetBlightStateSize(void) { return level.blight.cells ? G_BlightCellCount() : 0; }

BOOL G_GetBlightState(LPBYTE out, DWORD size) {
    DWORD const expected = G_GetBlightStateSize();
    if (size != expected || (size && !out)) return false;
    if (size) memcpy(out, level.blight.cells, size);
    return true;
}

BOOL G_SetBlightState(BYTE const *data, DWORD size) {
    DWORD const expected = G_GetBlightStateSize();
    if (size != expected || (size && !data)) return false;
    if (size) memcpy(level.blight.cells, data, size);
    return true;
}
